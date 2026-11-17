package bismuth.companion;

import android.app.Activity;
import android.content.ComponentName;
import android.content.ServiceConnection;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.IBinder;
import android.os.RemoteException;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import rikka.shizuku.Shizuku;

public final class MainActivity extends Activity {
    private static final int REQUEST_PERMISSION = 41;
    private TextView status;
    private IBismuthUserService service;
    private boolean bound;
    private final Shizuku.OnBinderReceivedListener binderReceived = this::refreshStatus;
    private final Shizuku.OnBinderDeadListener binderDead = () -> show("Shizuku disconnected. Restart Shizuku and reopen this app.");
    private final Shizuku.OnRequestPermissionResultListener permissionResult = (code, result) -> {
        if (code == REQUEST_PERMISSION && result == PackageManager.PERMISSION_GRANTED) bindTelemetryService();
        else show("Shizuku permission was not granted.");
    };
    private final ServiceConnection connection = new ServiceConnection() {
        @Override public void onServiceConnected(ComponentName name, IBinder binder) {
            service = IBismuthUserService.Stub.asInterface(binder);
            bound = true;
            try {
                service.startBridge();
                show(service.getStatus() + "\nKeep this app's bridge running while Geometry Dash is open.");
            } catch (RemoteException e) { show("Could not start telemetry bridge: " + e); }
        }
        @Override public void onServiceDisconnected(ComponentName name) {
            service = null; bound = false; show("Shizuku UserService disconnected.");
        }
    };

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(28, 32, 28, 24);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        TextView title = new TextView(this);
        title.setText("BISMUTH COMPANION");
        title.setTextSize(22);
        root.addView(title);
        status = new TextView(this);
        status.setTextSize(15);
        status.setPadding(0, 24, 0, 24);
        root.addView(status);
        Button start = new Button(this);
        start.setText("Connect to Shizuku & start bridge");
        start.setOnClickListener(v -> requestAndStart());
        root.addView(start);
        Button stop = new Button(this);
        stop.setText("Stop bridge");
        stop.setOnClickListener(v -> stopBridge());
        root.addView(stop);
        TextView note = new TextView(this);
        note.setText("Android 15 / Galaxy A05 target. The bridge listens only on 127.0.0.1:38471. GPU and thermal values may be unavailable if Samsung's kernel does not expose readable counters.");
        note.setPadding(0, 20, 0, 0);
        root.addView(note);
        setContentView(root);
        Shizuku.addBinderReceivedListenerSticky(binderReceived);
        Shizuku.addBinderDeadListener(binderDead);
        Shizuku.addRequestPermissionResultListener(permissionResult);
        refreshStatus();
    }

    private void requestAndStart() {
        try {
            if (!Shizuku.pingBinder()) { show("Shizuku is not running. Start it first."); return; }
            if (Shizuku.isPreV11()) { show("This app requires Shizuku API v11 or newer."); return; }
            if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) bindTelemetryService();
            else Shizuku.requestPermission(REQUEST_PERMISSION);
        } catch (Throwable e) { show("Shizuku error: " + e); }
    }

    private void bindTelemetryService() {
        try {
            Shizuku.UserServiceArgs args = new Shizuku.UserServiceArgs(
                new ComponentName(getPackageName(), UserService.class.getName()))
                .tag("bismuth-telemetry-v1").daemon(true).processNameSuffix("telemetry")
                .debuggable(BuildConfig.DEBUG).version(BuildConfig.VERSION_CODE);
            Shizuku.bindUserService(args, connection);
            show("Starting privileged telemetry service…");
        } catch (Throwable e) { show("Could not bind Shizuku UserService: " + e); }
    }

    private void stopBridge() {
        try {
            if (service != null) service.stopBridge();
            show("Bridge stopped.");
        } catch (RemoteException e) { show("Stop failed: " + e); }
    }

    private void refreshStatus() {
        runOnUiThread(() -> {
            try {
                if (!Shizuku.pingBinder()) show("Shizuku is not running.");
                else show("Shizuku connected. Permission: " +
                    (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED ? "granted" : "not granted"));
            } catch (Throwable e) { show("Waiting for Shizuku binder…"); }
        });
    }
    private void show(String text) { if (status != null) status.setText(text); }

    @Override protected void onDestroy() {
        Shizuku.removeBinderReceivedListener(binderReceived);
        Shizuku.removeBinderDeadListener(binderDead);
        Shizuku.removeRequestPermissionResultListener(permissionResult);
        // The UserService is daemonized intentionally; stop it explicitly with the Stop button.
        super.onDestroy();
    }
}
