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
    @Override public String getStatus() { return status; }

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
    private static double gpuLoad() {
        File[] dirs=new File("/sys/class/devfreq").listFiles(); if(dirs==null)return -1;
        for(File d:dirs) try {
            String n=d.getName().toLowerCase(Locale.ROOT);
            File nf=new File(d,"name"); if(nf.isFile())n+=" "+readText(nf.getPath()).toLowerCase(Locale.ROOT);
            if(!n.contains("gpu")&&!n.contains("mali"))continue;
            long busy=readLong(new File(d,"busy_time").getPath()), total=readLong(new File(d,"total_time").getPath());
            if(busy<0||total<=0)continue;
            long pb=previousGpuBusy,pt=previousGpuTotal;previousGpuBusy=busy;previousGpuTotal=total;
            if(pt==0||total<=pt||busy<pb)return -1;
            return Math.max(0,Math.min(100,(busy-pb)*100.0/(total-pt)));
        }catch(Exception ignored){}
        return -1;
    }
    private static long previousGpuBusy, previousGpuTotal;
    private static double temperature(String wanted) {
        File[] zones=new File("/sys/class/thermal").listFiles((d,n)->n.startsWith("thermal_zone"));
        if(zones==null)return -1; double hottest=-1;
        for(File z:zones)try{
            String type=readText(new File(z,"type").getPath()).toLowerCase(Locale.ROOT);
            if(!type.contains(wanted))continue;
            long raw=readLong(new File(z,"temp").getPath());
            if(raw>0&&raw<150000)hottest=Math.max(hottest,raw/1000.0);
        }catch(Exception ignored){}
        return hottest;
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
