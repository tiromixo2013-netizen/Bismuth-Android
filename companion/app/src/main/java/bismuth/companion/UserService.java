package bismuth.companion;

import android.os.RemoteException;
import android.system.Os;
import android.util.Log;
import androidx.annotation.Keep;
import java.io.*;
import java.net.*;
import java.util.Locale;

public final class UserService extends IBismuthUserService.Stub {
    private static final String TAG = "BismuthShizuku";
    private static final int PORT = 38471;
    private volatile boolean running;
    private volatile int generation;
    private volatile ServerSocket serverSocket;
    private Thread worker;
    private volatile String status = "Shizuku service ready; bridge stopped";
    public UserService() {}
    @Keep public UserService(android.content.Context ignored) {}

    @Override public synchronized void startBridge() {
        if (running) return;
        running = true;
        final int runId = ++generation;
        status = "Bridge starting on 127.0.0.1:" + PORT + " (uid " + Os.getuid() + ")";
        worker = new Thread(() -> serve(runId), "BismuthTelemetryBridge-" + runId);
        worker.start();
    }
    @Override public synchronized void stopBridge() {
        running = false;
        ++generation; // Invalidate the old worker before a rapid restart can begin.
        ServerSocket current = serverSocket;
        serverSocket = null;
        if (current != null) {
            try { current.close(); } catch (IOException ignored) {}
        }
        if (worker != null) worker.interrupt();
        worker = null;
        status = "Bridge stopped";
    }
    private boolean isCurrentRun(int runId) {
        return running && generation == runId;
    }
    @Override public String getStatus() { return status + "\\n\\n" + thermalDiagnostics(); }

    private void serve(int runId) {
        ServerSocket server = null;
        try {
            server = new ServerSocket();
            server.setReuseAddress(true);
            synchronized (this) {
                if (!isCurrentRun(runId)) return;
                serverSocket = server;
            }
            server.bind(new InetSocketAddress(InetAddress.getByName("127.0.0.1"), PORT), 4);
            server.setSoTimeout(1000);
            if (isCurrentRun(runId)) {
                status = "Bridge listening on 127.0.0.1:" + PORT + " (uid " + Os.getuid() + "); waiting for Bismuth";
                Log.i(TAG, status);
            }
            while (isCurrentRun(runId)) {
                try (Socket client = server.accept()) {
                    if (!isCurrentRun(runId)) break;
                    client.setTcpNoDelay(true);
                    BufferedWriter out = new BufferedWriter(new OutputStreamWriter(client.getOutputStream()));
                    status = "Bismuth connected; streaming shell telemetry";
                    Log.i(TAG, status);
                    while (isCurrentRun(runId) && !client.isClosed()) {
                        out.write(snapshotCsv()); out.newLine(); out.flush();
                        Thread.sleep(500);
                    }
                    if (isCurrentRun(runId)) status = "Bismuth client closed; bridge listening on 127.0.0.1:" + PORT;
                } catch (SocketTimeoutException ignored) {
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt(); break;
                } catch (IOException e) {
                    if (isCurrentRun(runId)) {
                        status = "Client I/O error: " + e.getClass().getSimpleName() + "; bridge listening";
                        Log.w(TAG, status, e);
                    }
                }
            }
        } catch (Exception e) {
            if (isCurrentRun(runId)) {
                status = "Bridge error: " + e.getClass().getSimpleName() + ": " + e.getMessage();
                Log.e(TAG, status, e);
            }
        } finally {
            if (server != null) {
                synchronized (this) {
                    if (serverSocket == server) serverSocket = null;
                }
                try { server.close(); } catch (IOException ignored) {}
            }
            // An obsolete worker must never change the state of a newer run.
            if (generation == runId) {
                running = false;
                worker = null;
                if (!"Bridge stopped".equals(status)) status = "Bridge stopped";
            }
        }
    }

    private static long readLong(String path) {
        try (BufferedReader r = new BufferedReader(new FileReader(path))) { return Long.parseLong(r.readLine().trim()); }
        catch (Exception ignored) { return -1; }
    }
    private static double cpuLoad() {
        try (BufferedReader r = new BufferedReader(new FileReader("/proc/stat"))) {
            String[] p = r.readLine().trim().split("\\s+");
            long total=0; long[] v=new long[p.length-1];
            for(int i=1;i<p.length;i++){v[i-1]=Long.parseLong(p[i]);total+=v[i-1];}
            long idle=v[3]+(v.length>4?v[4]:0), busy=total-idle;
            long pt=previousCpuTotal,pb=previousCpuBusy; previousCpuTotal=total; previousCpuBusy=busy;
            if(pt==0||total<=pt)return -1;
            return Math.max(0,Math.min(100,(busy-pb)*100.0/(total-pt)));
        } catch(Exception ignored){return -1;}
    }
    private static long previousCpuTotal, previousCpuBusy;
    private static String readText(String path) throws IOException {
        try(BufferedReader r=new BufferedReader(new FileReader(path))){return r.readLine();}
    }
    // GPU sysfs differs between Mali driver generations and MediaTek kernel builds.
    // Prefer a driver's already-computed percentage; fall back to devfreq time counters.
    private static String previousGpuSource = "";
    private static long previousGpuBusy, previousGpuTotal;

    private static double parseGpuPercent(String raw) {
        if (raw == null) return -1;
        String value = raw.trim();
        if (value.isEmpty()) return -1;
        // GED/Mali vendor nodes sometimes return labels such as "GPU loading: 37%".
        int percent = value.indexOf('%');
        if (percent >= 0) {
            int start = percent - 1;
            while (start >= 0 && (Character.isDigit(value.charAt(start)) ||
                    value.charAt(start) == '.' || value.charAt(start) == ' ')) start--;
            String token = value.substring(start + 1, percent).trim();
            try {
                double parsed = Double.parseDouble(token);
                return (Double.isFinite(parsed) && parsed >= 0 && parsed <= 100) ? parsed : -1;
            } catch (Exception ignored) { return -1; }
        }
        // Plain numeric utilization nodes are also common.
        try {
            double parsed = Double.parseDouble(value);
            return (Double.isFinite(parsed) && parsed >= 0 && parsed <= 100) ? parsed : -1;
        } catch (Exception ignored) { return -1; }
    }

    private static double readGpuPercent(String path) {
        try { return parseGpuPercent(readText(path)); }
        catch (Exception ignored) { return -1; }
    }

    // Qualcomm KGSL gpubusy reports busy time followed by elapsed time.
    private static double readGpuBusyPair(String path) {
        try {
            String raw = readText(path).trim();
            java.util.StringTokenizer values = new java.util.StringTokenizer(raw);
            if (values.countTokens() != 2) return -1;
            double busy = Double.parseDouble(values.nextToken());
            double total = Double.parseDouble(values.nextToken());
            if (!Double.isFinite(busy) || !Double.isFinite(total) || busy < 0 || total <= 0 || busy > total) return -1;
            return Math.max(0, Math.min(100, busy * 100.0 / total));
        } catch (Exception ignored) { return -1; }
    }

    private static double kgslLoad() {
        File root = new File("/sys/class/kgsl");
        File[] devices = root.listFiles();
        if (devices == null) return -1;
        for (File device : devices) {
            String name = device.getName().toLowerCase(Locale.ROOT);
            if (!name.contains("kgsl") && !name.contains("3d")) continue;
            for (String attr : new String[]{"gpu_busy_percentage", "gpu_busy", "utilization", "utilisation"}) {
                double value = readGpuPercent(new File(device, attr).getPath());
                if (value >= 0) return value;
            }
            double value = readGpuBusyPair(new File(device, "gpubusy").getPath());
            if (value >= 0) return value;
        }
        return -1;
    }

    private static double gpuCounterLoad(File dir) {
        File busyFile = new File(dir, "busy_time");
        File totalFile = new File(dir, "total_time");
        if (!busyFile.isFile() || !totalFile.isFile()) {
            File governor = new File(dir, "governor");
            busyFile = new File(governor, "busy_time");
            totalFile = new File(governor, "total_time");
        }
        if (!busyFile.isFile() || !totalFile.isFile()) return -1;
        long busy = readLong(busyFile.getPath()), total = readLong(totalFile.getPath());
        if (busy < 0 || total <= 0) return -1;
        String source = busyFile.getPath() + "|" + totalFile.getPath();
        if (!source.equals(previousGpuSource)) {
            previousGpuSource = source;
            previousGpuBusy = busy;
            previousGpuTotal = total;
            return -1;
        }
        long pb = previousGpuBusy, pt = previousGpuTotal;
        previousGpuBusy = busy;
        previousGpuTotal = total;
        if (pt <= 0 || total <= pt || busy < pb) return -1;
        return Math.max(0, Math.min(100, (busy - pb) * 100.0 / (total - pt)));
    }

    private static double gpuLoad() {
        // Qualcomm Adreno exposes utilization through KGSL on many Snapdragon kernels.
        double kgsl = kgslLoad();
        if (kgsl >= 0) return kgsl;

        // MediaTek GED (GPU Engine Driver) nodes used by several Mali/MTK kernels.
        // These may contain either a plain number or a labelled percentage.
        String[] ged = {
            "/sys/kernel/ged/hal/gpu_utilization",
            "/sys/kernel/ged/hal/gpu_loading",
            "/sys/kernel/ged/hal/gpu_busy",
            "/sys/kernel/ged/hal/gpu_busy_percentage",
            "/sys/kernel/debug/ged/hal/gpu_utilization",
            "/sys/kernel/debug/ged/hal/gpu_loading"
        };
        for (String path : ged) {
            double value = readGpuPercent(path);
            if (value >= 0) return value;
        }

        // Common ARM Mali / MediaTek driver nodes. Availability and read permission
        // are kernel-specific; an unreadable or non-percentage value remains N/A.
        String[] direct = {
            "/sys/class/misc/mali0/device/utilisation",
            "/sys/class/misc/mali0/device/utilization",
            "/sys/class/misc/mali0/device/load",
            "/sys/class/misc/mali0/device/gpu_busy",
            "/sys/class/misc/mali0/device/gpu_utilization",
            "/sys/class/misc/mali0/device/gpu_loading",
            "/sys/devices/platform/mali/utilisation",
            "/sys/devices/platform/mali/utilization",
            "/sys/devices/platform/mali/gpu_utilization",
            "/sys/devices/platform/mali/gpu_loading"
        };
        for (String path : direct) {
            double value = readGpuPercent(path);
            if (value >= 0) return value;
        }

        // Some MediaTek kernels expose a Mali devfreq node below the Mali device.
        File maliDevfreq = new File("/sys/class/misc/mali0/device/devfreq");
        File[] maliNodes = maliDevfreq.listFiles();
        if (maliNodes != null) for (File node : maliNodes) {
            double value = readGpuPercent(new File(node, "load").getPath());
            if (value >= 0) return value;
            value = readGpuPercent(new File(node, "utilisation").getPath());
            if (value >= 0) return value;
            value = gpuCounterLoad(node);
            if (value >= 0) return value;
        }

        // Generic devfreq is used by many MediaTek Mali integrations, but counter
        // files may live either at the device root or in its governor directory.
        File[] dirs = new File("/sys/class/devfreq").listFiles();
        if (dirs != null) for (File d : dirs) {
            try {
                String n = d.getName().toLowerCase(Locale.ROOT);
                File nf = new File(d, "name");
                if (nf.isFile()) n += " " + readText(nf.getPath()).toLowerCase(Locale.ROOT);
                if (!n.contains("gpu") && !n.contains("mali")) continue;
                double value = readGpuPercent(new File(d, "load").getPath());
                if (value >= 0) return value;
                value = readGpuPercent(new File(d, "utilisation").getPath());
                if (value >= 0) return value;
                value = readGpuPercent(new File(d, "utilization").getPath());
                if (value >= 0) return value;
                value = gpuCounterLoad(d);
                if (value >= 0) return value;
                // Some MTK kernels expose the devfreq device through a symlink
                // and keep the counters one level below its device directory.
                File device = new File(d, "device");
                value = readGpuPercent(new File(device, "load").getPath());
                if (value >= 0) return value;
                value = gpuCounterLoad(device);
                if (value >= 0) return value;
            } catch (Exception ignored) {}
        }

        // MediaTek commonly names the platform node with an address, e.g. *.mali.
        File[] platform = new File("/sys/devices/platform").listFiles();
        if (platform != null) for (File d : platform) {
            String n = d.getName().toLowerCase(Locale.ROOT);
            if (!n.contains("mali") && !n.contains("gpu")) continue;
            for (String attr : new String[]{"utilisation", "utilization", "load",
                    "gpu_utilization", "gpu_loading", "gpu_busy", "gpu_busy_percentage"}) {
                double value = readGpuPercent(new File(d, attr).getPath());
                if (value >= 0) return value;
            }
            // Some MTK trees expose counters under the platform device itself.
            double rootCounters = gpuCounterLoad(d);
            if (rootCounters >= 0) return rootCounters;
            File devfreq = new File(d, "devfreq");
            File[] nodes = devfreq.listFiles();
            if (nodes != null) for (File node : nodes) {
                for (String attr : new String[]{"load", "utilisation", "utilization"}) {
                    double value = readGpuPercent(new File(node, attr).getPath());
                    if (value >= 0) return value;
                }
                double value = gpuCounterLoad(node);
                if (value >= 0) return value;
                value = gpuCounterLoad(new File(node, "device"));
                if (value >= 0) return value;
            }
        }
        return -1;
    }
    private static boolean isWantedThermalType(String type, String wanted) {
        String n = type.toLowerCase(Locale.ROOT);
        if ("cpu".equals(wanted)) return n.contains("cpu") || n.contains("mtktscpu");
        if ("gpu".equals(wanted)) return n.contains("gpu") || n.contains("mali") || n.contains("gpuss");
        return n.contains(wanted);
    }

    private static double normalizeTemperature(long raw) {
        if (raw <= 0) return -1;
        if (raw < 150000) return raw / 1000.0; // millidegrees Celsius
        if (raw < 150000000) return raw / 1000000.0; // microdegrees Celsius
        return -1;
    }

    private static double temperature(String wanted) {
        double hottest = -1;
        File[] zones = new File("/sys/class/thermal").listFiles((d,n) -> n.startsWith("thermal_zone"));
        if (zones != null) for (File z : zones) try {
            String type = readText(new File(z, "type").getPath());
            if (!isWantedThermalType(type, wanted)) continue;
            double value = normalizeTemperature(readLong(new File(z, "temp").getPath()));
            if (value > 0 && value < 150) hottest = Math.max(hottest, value);
        } catch (Exception ignored) {}

        // Some vendor kernels publish labelled sensors through hwmon instead of thermal_zone.
        File[] hwmons = new File("/sys/class/hwmon").listFiles((d,n) -> n.startsWith("hwmon"));
        if (hwmons != null) for (File hw : hwmons) try {
            String hwName = "";
            File nameFile = new File(hw, "name");
            if (nameFile.isFile()) hwName = readText(nameFile.getPath());
            File[] inputs = hw.listFiles((d,n) -> n.matches("temp[0-9]+_input"));
            if (inputs == null) continue;
            for (File input : inputs) {
                String index = input.getName().replace("_input", "");
                File label = new File(hw, index + "_label");
                String sensorName = hwName;
                if (label.isFile()) sensorName += " " + readText(label.getPath());
                if (!isWantedThermalType(sensorName, wanted)) continue;
                double value = normalizeTemperature(readLong(input.getPath()));
                if (value > 0 && value < 150) hottest = Math.max(hottest, value);
            }
        } catch (Exception ignored) {}
        return hottest;
    }

    private static String thermalDiagnostics() {
        StringBuilder out = new StringBuilder("Thermal diagnostics:");
        File[] zones = new File("/sys/class/thermal").listFiles((d,n) -> n.startsWith("thermal_zone"));
        int count = 0;
        if (zones != null) for (File z : zones) {
            if (count++ >= 18) { out.append("\\n…"); break; }
            try {
                String type = readText(new File(z, "type").getPath());
                String raw = readText(new File(z, "temp").getPath());
                out.append("\\n").append(z.getName()).append(" [").append(type).append("]=").append(raw);
            } catch (Exception e) { out.append("\\n").append(z.getName()).append(" [unreadable]"); }
        }
        if (count == 0) out.append("\\nNo readable thermal_zone entries");
        File[] hwmons = new File("/sys/class/hwmon").listFiles((d,n) -> n.startsWith("hwmon"));
        if (hwmons != null) for (File hw : hwmons) try {
            File name = new File(hw, "name");
            if (name.isFile()) out.append("\\nhwmon ").append(hw.getName()).append(": ").append(readText(name.getPath()));
        } catch (Exception ignored) {}
        out.append("\\nMatched CPU=").append(temperature("cpu")).append("C GPU=").append(temperature("gpu")).append("C");
        return out.toString();
    }
    private static long[] memory() {
        try(BufferedReader r=new BufferedReader(new FileReader("/proc/meminfo"))){
            long total=-1,available=-1;String line;
            while((line=r.readLine())!=null){
                if(line.startsWith("MemTotal:"))total=Long.parseLong(line.replaceAll("[^0-9]",""));
                if(line.startsWith("MemAvailable:"))available=Long.parseLong(line.replaceAll("[^0-9]",""));
            }
            if(total>0&&available>=0)return new long[]{(total-available)/1024,total/1024};
        }catch(Exception ignored){}
        return new long[]{-1,-1};
    }
    private static String snapshotCsv(){
        double cpu=cpuLoad(),gpu=gpuLoad(),ct=temperature("cpu"),gt=temperature("gpu");
        long[] mem=memory();
        return String.format(Locale.US,"%.2f,%.2f,%.2f,%.2f,%d,%d",cpu,gpu,ct,gt,mem[0],mem[1]);
    }
    @Override public void destroy(){stopBridge();System.exit(0);}
}
