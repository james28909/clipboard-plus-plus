package com.clipboardplusplus.androidapi;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.os.SystemClock;
import android.view.accessibility.AccessibilityEvent;

public class ClipboardPpAccessibilityService extends AccessibilityService {
    private static final long SYNC_DEBOUNCE_MS = 900;
    private long lastSyncAt;
    private ClipboardBridge bridge;

    @Override
    protected void onServiceConnected() {
        super.onServiceConnected();
        bridge = ClipboardBridge.get(this);
        ApiServer.get(this).start();

        AccessibilityServiceInfo info = new AccessibilityServiceInfo();
        info.eventTypes = AccessibilityEvent.TYPE_VIEW_CLICKED |
                AccessibilityEvent.TYPE_NOTIFICATION_STATE_CHANGED;
        info.feedbackType = AccessibilityServiceInfo.FEEDBACK_GENERIC;
        info.notificationTimeout = 120;
        setServiceInfo(info);
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent event) {
        if (event == null || getPackageName().contentEquals(event.getPackageName())) {
            return;
        }
        if (looksLikeClipboardAction(event)) {
            requestSync("accessibility-copy");
        }
    }

    @Override
    public void onInterrupt() {
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
    }

    private boolean looksLikeClipboardAction(AccessibilityEvent event) {
        switch (event.getEventType()) {
            case AccessibilityEvent.TYPE_VIEW_CLICKED:
            case AccessibilityEvent.TYPE_NOTIFICATION_STATE_CHANGED:
                return hasCopyText(event);
            default:
                return false;
        }
    }

    private boolean hasCopyText(AccessibilityEvent event) {
        if (containsCopyWord(event.getContentDescription())) {
            return true;
        }
        for (CharSequence text : event.getText()) {
            if (containsCopyWord(text)) {
                return true;
            }
        }
        return false;
    }

    private boolean containsCopyWord(CharSequence value) {
        if (value == null) {
            return false;
        }
        String lower = value.toString().trim().toLowerCase();
        if (lower.length() > 40) {
            return false;
        }
        return lower.contains("copy") ||
                lower.contains("copied") ||
                lower.contains("cut") ||
                lower.contains("clipboard");
    }

    private void requestSync(String source) {
        long now = SystemClock.elapsedRealtime();
        if (now - lastSyncAt < SYNC_DEBOUNCE_MS) {
            return;
        }
        lastSyncAt = now;
        if (bridge == null) {
            bridge = ClipboardBridge.get(this);
        }
        bridge.requestForegroundSync(source);
    }
}
