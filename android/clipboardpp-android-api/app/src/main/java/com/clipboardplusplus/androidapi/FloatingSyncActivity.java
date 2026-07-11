package com.clipboardplusplus.androidapi;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.TextView;

public class FloatingSyncActivity extends Activity implements ClipboardBridge.Listener {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private TextView status;
    private ClipboardBridge bridge;
    private boolean syncStarted;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        status = new TextView(this);
        status.setText("Syncing Clipboard++...");
        status.setGravity(Gravity.CENTER);
        status.setPadding(32, 24, 32, 24);
        setContentView(status);

        bridge = ClipboardBridge.get(this);
        bridge.addListener(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (syncStarted) {
            return;
        }
        syncStarted = true;
        handler.postDelayed(() -> {
            if (bridge != null) {
                bridge.syncCurrentClipboardToWindowsBlocking("floating-activity", this);
                status.setText("Clipboard++ sync requested");
            }
        }, 180);

        handler.postDelayed(() -> {
            moveTaskToBack(true);
            finishAndRemoveTask();
        }, 2600);
    }

    @Override
    protected void onDestroy() {
        if (bridge != null) {
            bridge.removeListener(this);
        }
        super.onDestroy();
    }

    @Override
    public void onBridgeChanged() {
        if (status != null && bridge != null) {
            status.setText(bridge.getLastStatus() + "\n" + bridge.getLastPushStatus());
        }
    }
}
