package com.clipboardplusplus.androidapi;

import android.content.ClipboardManager;
import android.content.Context;
import android.inputmethodservice.InputMethodService;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public class ClipboardPpImeService extends InputMethodService implements ClipboardBridge.Listener {
    private ClipboardBridge bridge;
    private ClipboardManager clipboard;
    private ClipboardManager.OnPrimaryClipChangedListener clipListener;
    private TextView status;

    @Override
    public void onCreate() {
        super.onCreate();
        bridge = ClipboardBridge.get(this);
        clipboard = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
        clipListener = () -> bridge.captureCurrentClipboard("ime-listener");
        if (clipboard != null) {
            clipboard.addPrimaryClipChangedListener(clipListener);
        }
        bridge.addListener(this);
        ApiServer.get(this).start();
    }

    @Override
    public void onDestroy() {
        if (clipboard != null && clipListener != null) {
            clipboard.removePrimaryClipChangedListener(clipListener);
        }
        bridge.removeListener(this);
        super.onDestroy();
    }

    @Override
    public View onCreateInputView() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 18, 24, 18);

        status = new TextView(this);
        status.setTextSize(16);
        root.addView(status);

        LinearLayout buttons = new LinearLayout(this);
        buttons.setOrientation(LinearLayout.HORIZONTAL);

        Button sync = new Button(this);
        sync.setText("Sync now");
        sync.setOnClickListener(v -> bridge.captureCurrentClipboard("ime-sync"));
        buttons.addView(sync);

        Button picker = new Button(this);
        picker.setText("Switch keyboard");
        picker.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showInputMethodPicker();
            }
        });
        buttons.addView(picker);

        root.addView(buttons);
        refresh();
        return root;
    }

    @Override
    public void onStartInputView(android.view.inputmethod.EditorInfo info, boolean restarting) {
        super.onStartInputView(info, restarting);
        bridge.captureCurrentClipboard("ime-start");
        refresh();
    }

    @Override
    public void onWindowShown() {
        super.onWindowShown();
        bridge.captureCurrentClipboard("ime-window");
        refresh();
    }

    @Override
    public void onBridgeChanged() {
        refresh();
    }

    private void refresh() {
        if (status == null) {
            return;
        }
        status.setText("Clipboard++ capture keyboard\n" +
                bridge.getLastStatus() +
                "\nCaptured items: " + bridge.snapshot().size());
    }
}
