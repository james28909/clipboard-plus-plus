package com.clipboardplusplus.androidapi;

import android.app.Service;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.net.Uri;
import android.os.Build;
import android.os.IBinder;
import android.provider.Settings;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.TextView;

public class FloatingSyncService extends Service {
    private WindowManager windowManager;
    private TextView bubble;
    private WindowManager.LayoutParams params;
    private ClipboardBridge bridge;
    private float downX;
    private float downY;
    private int startX;
    private int startY;
    private boolean moved;

    @Override
    public void onCreate() {
        super.onCreate();
        bridge = ClipboardBridge.get(this);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            stopSelf();
            return;
        }

        windowManager = (WindowManager) getSystemService(WINDOW_SERVICE);
        bubble = new TextView(this);
        bubble.setText("C++");
        bubble.setTextColor(Color.WHITE);
        bubble.setTextSize(14);
        bubble.setGravity(Gravity.CENTER);
        bubble.setBackgroundColor(Color.rgb(36, 96, 190));
        bubble.setPadding(18, 14, 18, 14);

        params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                        ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
                        : WindowManager.LayoutParams.TYPE_PHONE,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT);
        params.gravity = Gravity.TOP | Gravity.START;
        params.x = 24;
        params.y = 240;

        bubble.setOnTouchListener(this::onBubbleTouch);
        windowManager.addView(bubble, params);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (windowManager != null && bubble != null) {
            windowManager.removeView(bubble);
        }
        bubble = null;
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private boolean onBubbleTouch(View view, MotionEvent event) {
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downX = event.getRawX();
                downY = event.getRawY();
                startX = params.x;
                startY = params.y;
                moved = false;
                return true;
            case MotionEvent.ACTION_MOVE:
                int dx = Math.round(event.getRawX() - downX);
                int dy = Math.round(event.getRawY() - downY);
                if (Math.abs(dx) > 6 || Math.abs(dy) > 6) {
                    moved = true;
                    params.x = startX + dx;
                    params.y = startY + dy;
                    windowManager.updateViewLayout(bubble, params);
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!moved) {
                    bubble.setText("...");
                    Intent sync = new Intent(this, FloatingSyncActivity.class);
                    sync.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                            Intent.FLAG_ACTIVITY_MULTIPLE_TASK |
                            Intent.FLAG_ACTIVITY_NO_HISTORY |
                            Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
                    startActivity(sync);
                    bubble.setText("OK");
                    bubble.postDelayed(() -> {
                        if (bubble != null) {
                            bubble.setText("C++");
                        }
                    }, 900);
                }
                return true;
            default:
                return false;
        }
    }

    static Intent overlayPermissionIntent(Service service) {
        Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + service.getPackageName()));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        return intent;
    }
}
