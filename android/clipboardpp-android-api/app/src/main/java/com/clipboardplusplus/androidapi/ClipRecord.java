package com.clipboardplusplus.androidapi;

import org.json.JSONException;
import org.json.JSONObject;

final class ClipRecord {
    final String id;
    final String text;
    final long capturedAtMs;
    final String source;
    boolean pinned;
    boolean hidden;
    boolean pushedToWindows;

    ClipRecord(String id, String text, long capturedAtMs, String source, boolean pinned, boolean hidden, boolean pushedToWindows) {
        this.id = id;
        this.text = text;
        this.capturedAtMs = capturedAtMs;
        this.source = source;
        this.pinned = pinned;
        this.hidden = hidden;
        this.pushedToWindows = pushedToWindows;
    }

    JSONObject toJson(boolean revealHidden) throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("id", id);
        obj.put("text", hidden && !revealHidden ? "" : text);
        obj.put("capturedAtMs", capturedAtMs);
        obj.put("source", source);
        obj.put("pinned", pinned);
        obj.put("hidden", hidden);
        obj.put("pushedToWindows", pushedToWindows);
        return obj;
    }

    static ClipRecord fromJson(JSONObject obj) {
        return new ClipRecord(
                obj.optString("id"),
                obj.optString("text"),
                obj.optLong("capturedAtMs"),
                obj.optString("source", "unknown"),
                obj.optBoolean("pinned", false),
                obj.optBoolean("hidden", false),
                obj.optBoolean("pushedToWindows", false));
    }
}
