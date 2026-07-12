package com.clipboardplusplus.androidapi;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;

import java.util.List;

public class MainActivity extends Activity implements ClipboardBridge.Listener {
    private ClipboardBridge bridge;
    private TextView status;
    private TextView items;
    private EditText windowsEndpoint;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        bridge = ClipboardBridge.get(this);
        ApiServer.get(this).start();
        bridge.captureCurrentClipboard("activity-launch");

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(32, 32, 32, 32);

        TextView title = new TextView(this);
        title.setText("Clipboard++ Android API");
        title.setTextSize(22);
        root.addView(title);

        TextView explainer = new TextView(this);
        explainer.setText("This app is a Clipboard++ companion. Enable accessibility sync, set the Windows endpoint, and copied Android text will auto-push to Clipboard++.");
        root.addView(explainer);

        status = new TextView(this);
        root.addView(status);

        Button enableKeyboard = new Button(this);
        enableKeyboard.setText("Enable Keyboard");
        enableKeyboard.setOnClickListener(v -> startActivity(new Intent(Settings.ACTION_INPUT_METHOD_SETTINGS)));
        root.addView(enableKeyboard);

        Button enableAccessibility = new Button(this);
        enableAccessibility.setText("Enable Accessibility Sync");
        enableAccessibility.setOnClickListener(v -> startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)));
        root.addView(enableAccessibility);

        Button pickKeyboard = new Button(this);
        pickKeyboard.setText("Pick Keyboard");
        pickKeyboard.setOnClickListener(v -> {
            InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showInputMethodPicker();
            }
        });
        root.addView(pickKeyboard);

        Button syncNow = new Button(this);
        syncNow.setText("Sync Current Clipboard Now");
        syncNow.setOnClickListener(v -> {
            bridge.requestForegroundSync("activity-button");
            refresh();
        });
        root.addView(syncNow);

        Switch preserve = new Switch(this);
        preserve.setText("Preserve captured Android history");
        preserve.setChecked(bridge.getPreserveHistory());
        preserve.setOnCheckedChangeListener((buttonView, isChecked) -> bridge.setPreserveHistory(isChecked));
        root.addView(preserve);

        Switch pushToWindows = new Switch(this);
        pushToWindows.setText("Push captured items to Windows Clipboard++");
        pushToWindows.setChecked(bridge.getPushToWindows());
        pushToWindows.setOnCheckedChangeListener((buttonView, isChecked) -> bridge.setPushToWindows(isChecked));
        root.addView(pushToWindows);

        windowsEndpoint = new EditText(this);
        windowsEndpoint.setSingleLine(true);
        windowsEndpoint.setHint("http://WINDOWS-PC-IP:8766");
        windowsEndpoint.setText(bridge.getWindowsEndpoint());
        root.addView(windowsEndpoint);

        Button saveEndpoint = new Button(this);
        saveEndpoint.setText("Save Windows Endpoint");
        saveEndpoint.setOnClickListener(v -> {
            bridge.setWindowsEndpoint(windowsEndpoint.getText().toString());
            refresh();
        });
        root.addView(saveEndpoint);

        Button pushAll = new Button(this);
        pushAll.setText("Push All Captured Items to Windows Again");
        pushAll.setOnClickListener(v -> {
            bridge.pushAllToWindows();
            refresh();
        });
        root.addView(pushAll);

        Button overlayPermission = new Button(this);
        overlayPermission.setText("Enable Floating Button Permission");
        overlayPermission.setOnClickListener(v -> {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            }
        });
        root.addView(overlayPermission);

        Button showFloating = new Button(this);
        showFloating.setText("Show Floating Sync Button");
        showFloating.setOnClickListener(v -> {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(this)) {
                startService(new Intent(this, FloatingSyncService.class));
            } else {
                Intent intent = new Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivity(intent);
            }
        });
        root.addView(showFloating);

        Button hideFloating = new Button(this);
        hideFloating.setText("Hide Floating Sync Button");
        hideFloating.setOnClickListener(v -> stopService(new Intent(this, FloatingSyncService.class)));
        root.addView(hideFloating);

        TextView api = new TextView(this);
        api.setText(addressText());
        root.addView(api);

        items = new TextView(this);
        root.addView(items);

        ScrollView page = new ScrollView(this);
        page.addView(root);
        setContentView(page);
        refresh();
    }

    @Override
    protected void onStart() {
        super.onStart();
        bridge.addListener(this);
        bridge.captureCurrentClipboard("activity-start");
    }

    @Override
    protected void onStop() {
        bridge.removeListener(this);
        super.onStop();
    }

    @Override
    public void onBridgeChanged() {
        refresh();
    }

    private void refresh() {
        status.setText("Status: " + bridge.getLastStatus() +
                "\nWindows push: " + bridge.getLastPushStatus() +
                "\nCaptured items: " + bridge.snapshot().size() +
                "\nAPI running: " + ApiServer.get(this).isRunning());

        StringBuilder sb = new StringBuilder();
        List<ClipRecord> snapshot = bridge.snapshot();
        if (snapshot.isEmpty()) {
            sb.append("No captured items yet.");
        } else {
            for (ClipRecord record : snapshot) {
                sb.append(record.pinned ? "[pinned] " : "");
                sb.append(record.source).append(" - ").append(record.id).append("\n");
                sb.append(record.hidden ? "<hidden>" : record.text).append("\n\n");
            }
        }
        items.setText(sb.toString());
    }

    private static String addressText() {
        StringBuilder sb = new StringBuilder("Local API:\n");
        List<String> addresses = ApiServer.localAddresses();
        if (addresses.isEmpty()) {
            sb.append("Connect phone to Wi-Fi to see an address.\n");
        } else {
            for (String address : addresses) {
                sb.append(address).append("\n");
            }
        }
        return sb.toString();
    }
}
