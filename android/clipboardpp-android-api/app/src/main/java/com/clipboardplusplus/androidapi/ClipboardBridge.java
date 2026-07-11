package com.clipboardplusplus.androidapi;

import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;

final class ClipboardBridge {
    interface Listener {
        void onBridgeChanged();
    }

    private static final String PREFS = "clipboardpp_bridge";
    private static final String KEY_ITEMS = "items";
    private static final String KEY_PRESERVE_HISTORY = "preserve_history";
    private static final String KEY_WINDOWS_ENDPOINT = "windows_endpoint";
    private static final String KEY_PUSH_TO_WINDOWS = "push_to_windows";
    private static ClipboardBridge sInstance;

    private final Context appContext;
    private final ClipboardManager clipboard;
    private final SharedPreferences prefs;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ArrayList<ClipRecord> items = new ArrayList<>();
    private final ArrayList<Listener> listeners = new ArrayList<>();

    private String lastStatus = "Ready";
    private String lastPushStatus = "No Windows push yet";
    private boolean preserveHistory = true;
    private String windowsEndpoint = "";
    private boolean pushToWindows = true;

    static synchronized ClipboardBridge get(Context context) {
        if (sInstance == null) {
            sInstance = new ClipboardBridge(context.getApplicationContext());
        }
        return sInstance;
    }

    private ClipboardBridge(Context context) {
        appContext = context;
        clipboard = (ClipboardManager) context.getSystemService(Context.CLIPBOARD_SERVICE);
        prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        load();
    }

    synchronized void addListener(Listener listener) {
        if (!listeners.contains(listener)) {
            listeners.add(listener);
        }
    }

    synchronized void removeListener(Listener listener) {
        listeners.remove(listener);
    }

    synchronized String getLastStatus() {
        return lastStatus;
    }

    synchronized String getLastPushStatus() {
        return lastPushStatus;
    }

    synchronized List<ClipRecord> snapshot() {
        return new ArrayList<>(items);
    }

    synchronized void setPreserveHistory(boolean preserve) {
        preserveHistory = preserve;
        prefs.edit().putBoolean(KEY_PRESERVE_HISTORY, preserveHistory).apply();
        if (!preserveHistory && items.size() > 1) {
            ClipRecord newest = items.get(0);
            items.clear();
            items.add(newest);
            save();
            notifyChanged();
        }
    }

    synchronized boolean getPreserveHistory() {
        return preserveHistory;
    }

    synchronized String getWindowsEndpoint() {
        return windowsEndpoint;
    }

    synchronized void setWindowsEndpoint(String endpoint) {
        windowsEndpoint = endpoint == null ? "" : endpoint.trim();
        prefs.edit().putString(KEY_WINDOWS_ENDPOINT, windowsEndpoint).apply();
        notifyChanged();
    }

    synchronized boolean getPushToWindows() {
        return pushToWindows;
    }

    synchronized void setPushToWindows(boolean enabled) {
        pushToWindows = enabled;
        prefs.edit().putBoolean(KEY_PUSH_TO_WINDOWS, pushToWindows).apply();
        notifyChanged();
    }

    boolean captureCurrentClipboard(String source) {
        return captureCurrentClipboard(source, true);
    }

    boolean captureCurrentClipboard(String source, boolean autoPushNewItems) {
        try {
            if (clipboard == null || !clipboard.hasPrimaryClip()) {
                setStatus("No Android clipboard content available");
                return false;
            }

            ClipData data = clipboard.getPrimaryClip();
            if (data == null || data.getItemCount() == 0) {
                setStatus("Android denied clipboard read or clipboard is empty");
                return false;
            }

            int captured = 0;
            int duplicates = 0;
            for (int i = 0; i < data.getItemCount(); ++i) {
                CharSequence text = data.getItemAt(i).coerceToText(appContext);
                if (text == null || text.length() == 0) {
                    continue;
                }
                ClipRecord record = appendText(text.toString(), source, autoPushNewItems);
                if (record != null) {
                    ++captured;
                } else {
                    ++duplicates;
                }
            }

            if (captured == 0 && duplicates == 0) {
                setStatus("Clipboard changed, but no text content was readable");
                return false;
            }

            if (captured > 0) {
                setStatus("Captured " + captured + " clipboard item(s)");
                return true;
            }

            setStatus("Clipboard item(s) already captured");
            return false;
        } catch (SecurityException ex) {
            setStatus("Android denied clipboard read: " + ex.getClass().getSimpleName());
            return false;
        } catch (RuntimeException ex) {
            setStatus("Clipboard read failed: " + ex.getClass().getSimpleName());
            return false;
        }
    }

    synchronized ClipRecord appendText(String text, String source) {
        return appendText(text, source, true);
    }

    synchronized ClipRecord appendText(String text, String source, boolean autoPushNewItem) {
        if (findByText(text) != null)
            return null;

        ClipRecord record = new ClipRecord(
                UUID.randomUUID().toString(),
                text,
                System.currentTimeMillis(),
                source,
                false,
                false,
                "api".equals(source));

        if (!preserveHistory) {
            items.clear();
        }
        items.add(0, record);
        save();
        notifyChanged();
        if (autoPushNewItem && pushToWindows && !windowsEndpoint.isEmpty() && !"api".equals(source)) {
            pushRecordToWindows(record);
        }
        return record;
    }

    synchronized void pushAllToWindows() {
        if (windowsEndpoint.isEmpty()) {
            setPushStatus("Windows endpoint is empty");
            return;
        }
        if (items.isEmpty()) {
            setPushStatus("No captured items to push");
            return;
        }
        for (ClipRecord record : new ArrayList<>(items)) {
            pushRecordToWindows(record);
        }
        setPushStatus("Queued " + items.size() + " item(s) for Windows push");
    }

    void syncCurrentClipboardToWindowsBlocking(String source, Listener listener) {
        boolean captured = captureCurrentClipboard(source, false);
        String endpoint;
        synchronized (this) {
            endpoint = windowsEndpoint;
        }
        if (endpoint.isEmpty()) {
            setPushStatus("Windows endpoint is empty");
            return;
        }

        List<ClipRecord> capturedItems = capturedSnapshot();
        if (capturedItems.isEmpty()) {
            setPushStatus("No captured items to push");
            return;
        }

        new Thread(() -> {
            List<ClipRecord> missing = WindowsSyncClient.missingItemsBlocking(endpoint, capturedItems);
            List<ClipRecord> toPush = missing != null ? missing : unpushedSnapshot();
            if (toPush.isEmpty()) {
                markPushed(capturedItems);
                setPushStatus(captured
                        ? "Captured item already exists in Windows"
                        : "All captured items already exist in Windows");
                if (listener != null) {
                    main.post(listener::onBridgeChanged);
                }
                return;
            }

            boolean ok = WindowsSyncClient.pushItemsBlocking(endpoint, toPush);
            if (ok) {
                markPushed(toPush);
            }
            setPushStatus(ok
                    ? "Sync pushed " + toPush.size() + " missing item(s)"
                    : "Floating sync push failed");
            if (listener != null) {
                main.post(listener::onBridgeChanged);
            }
        }, "ClipboardPpFloatingSyncPush").start();
    }

    synchronized boolean setPinned(String id, boolean pinned) {
        ClipRecord record = findById(id);
        if (record == null) {
            return false;
        }
        record.pinned = pinned;
        save();
        notifyChanged();
        return true;
    }

    synchronized boolean remove(String id) {
        for (int i = 0; i < items.size(); ++i) {
            if (items.get(i).id.equals(id)) {
                items.remove(i);
                save();
                notifyChanged();
                return true;
            }
        }
        return false;
    }

    synchronized boolean reorder(List<String> ids) {
        ArrayList<ClipRecord> reordered = new ArrayList<>();
        Set<String> seen = new HashSet<>();
        for (String id : ids) {
            ClipRecord record = findById(id);
            if (record != null && seen.add(id)) {
                reordered.add(record);
            }
        }
        for (ClipRecord record : items) {
            if (seen.add(record.id)) {
                reordered.add(record);
            }
        }
        items.clear();
        items.addAll(reordered);
        save();
        notifyChanged();
        return true;
    }

    synchronized boolean setAndroidClipboardFromItem(String id) {
        ClipRecord record = findById(id);
        if (record == null) {
            return false;
        }
        setAndroidClipboard(record.text, false);
        return true;
    }

    void setAndroidClipboard(String text, boolean appendHistory) {
        if (clipboard == null) {
            setStatus("ClipboardManager unavailable");
            return;
        }
        clipboard.setPrimaryClip(ClipData.newPlainText("Clipboard++", text));
        if (appendHistory) {
            appendText(text, "api");
        }
        setStatus("Set Android system clipboard");
    }

    synchronized JSONArray toJsonArray(boolean revealHidden) throws JSONException {
        JSONArray array = new JSONArray();
        for (ClipRecord item : items) {
            array.put(item.toJson(revealHidden));
        }
        return array;
    }

    private synchronized ClipRecord findById(String id) {
        for (ClipRecord item : items) {
            if (item.id.equals(id)) {
                return item;
            }
        }
        return null;
    }

    private synchronized ClipRecord findByText(String text) {
        for (ClipRecord item : items) {
            if (item.text.equals(text)) {
                return item;
            }
        }
        return null;
    }

    private synchronized List<ClipRecord> capturedSnapshot() {
        return new ArrayList<>(items);
    }

    private synchronized List<ClipRecord> unpushedSnapshot() {
        ArrayList<ClipRecord> out = new ArrayList<>();
        for (ClipRecord item : items) {
            if (!item.pushedToWindows) {
                out.add(item);
            }
        }
        return out;
    }

    private synchronized void markPushed(List<ClipRecord> pushed) {
        HashSet<String> ids = new HashSet<>();
        for (ClipRecord record : pushed) {
            ids.add(record.id);
        }
        for (ClipRecord item : items) {
            if (ids.contains(item.id)) {
                item.pushedToWindows = true;
            }
        }
        save();
        notifyChanged();
    }

    private synchronized void setStatus(String status) {
        lastStatus = status;
        notifyChanged();
    }

    private void pushRecordToWindows(ClipRecord record) {
        setPushStatus("Pushing to Windows...");
        WindowsSyncClient.pushItem(windowsEndpoint, record, (ok, message) -> {
            if (ok) {
                markPushed(java.util.Collections.singletonList(record));
            }
            setPushStatus(message);
            if (!ok) {
                main.postDelayed(() ->
                        WindowsSyncClient.pushItem(windowsEndpoint, record,
                                (retryOk, retryMessage) -> {
                                    if (retryOk) {
                                        markPushed(java.util.Collections.singletonList(record));
                                    }
                                    setPushStatus("Retry: " + retryMessage);
                                }),
                        800);
            }
        });
    }

    private synchronized void setPushStatus(String status) {
        lastPushStatus = status;
        notifyChanged();
    }

    private synchronized void load() {
        items.clear();
        preserveHistory = prefs.getBoolean(KEY_PRESERVE_HISTORY, true);
        windowsEndpoint = prefs.getString(KEY_WINDOWS_ENDPOINT, "");
        pushToWindows = prefs.getBoolean(KEY_PUSH_TO_WINDOWS, true);
        String raw = prefs.getString(KEY_ITEMS, "[]");
        try {
            JSONArray array = new JSONArray(raw);
            for (int i = 0; i < array.length(); ++i) {
                JSONObject obj = array.optJSONObject(i);
                if (obj != null) {
                    items.add(ClipRecord.fromJson(obj));
                }
            }
        } catch (JSONException ignored) {
            items.clear();
        }
    }

    private synchronized void save() {
        JSONArray array = new JSONArray();
        for (ClipRecord item : items) {
            try {
                array.put(item.toJson(true));
            } catch (JSONException ignored) {
            }
        }
        prefs.edit().putString(KEY_ITEMS, array.toString()).apply();
    }

    private void notifyChanged() {
        ArrayList<Listener> copy;
        synchronized (this) {
            copy = new ArrayList<>(listeners);
        }
        main.post(() -> {
            for (Listener listener : copy) {
                listener.onBridgeChanged();
            }
        });
    }
}
