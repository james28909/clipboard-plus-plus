package com.clipboardplusplus.androidapi;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

public class FloatingSyncActivity extends Activity {
    static final String EXTRA_SOURCE = "com.clipboardplusplus.androidapi.SYNC_SOURCE";

    private final Handler handler = new Handler(Looper.getMainLooper());
    private ClipboardBridge bridge;
    private boolean syncStarted;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        View invisible = new View(this);
        invisible.setBackgroundColor(Color.TRANSPARENT);
        setContentView(invisible);

        Window window = getWindow();
        if (window != null) {
            window.setGravity(Gravity.TOP | Gravity.START);
            window.setDimAmount(0f);
            window.setLayout(1, 1);
            window.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
        }

        bridge = ClipboardBridge.get(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (syncStarted) {
            return;
        }
        syncStarted = true;
        String source = getIntent().getStringExtra(EXTRA_SOURCE);
        if (source == null || source.trim().isEmpty()) {
            source = "floating-activity";
        }
        final String syncSource = source;
        handler.postDelayed(() -> {
            if (bridge != null) {
                bridge.syncCurrentClipboardToWindowsBlocking(syncSource, null);
            }
        }, 180);

        handler.postDelayed(() -> {
            moveTaskToBack(true);
            finishAndRemoveTask();
        }, 2600);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }
}
