package com.clipboardplusplus.androidapi;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

final class WindowsSyncClient {
    interface Callback {
        void onComplete(boolean ok, String message);
    }

    private WindowsSyncClient() {
    }

    static List<ClipRecord> missingItemsBlocking(String endpoint, List<ClipRecord> records) {
        if (endpoint == null || endpoint.trim().isEmpty() || records == null || records.isEmpty()) {
            return new ArrayList<>();
        }

        HttpURLConnection connection = null;
        try {
            byte[] bytes = buildItemsPayload(records).toString().getBytes(StandardCharsets.UTF_8);

            connection = (HttpURLConnection) new URL(urlFor(endpoint, "/android/items/missing")).openConnection();
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(3000);
            connection.setReadTimeout(3000);
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            connection.setRequestProperty("Content-Length", Integer.toString(bytes.length));

            try (OutputStream out = connection.getOutputStream()) {
                out.write(bytes);
            }

            int code = connection.getResponseCode();
            if (code < 200 || code >= 300) {
                return null;
            }

            String response = readAll(connection.getInputStream());
            JSONArray missing = new JSONObject(response).optJSONArray("missing");
            ArrayList<ClipRecord> out = new ArrayList<>();
            if (missing == null) {
                return out;
            }

            for (int i = 0; i < missing.length(); ++i) {
                JSONObject item = missing.optJSONObject(i);
                String text = item == null ? "" : item.optString("text", "");
                for (ClipRecord record : records) {
                    if (record.text.equals(text) && !out.contains(record)) {
                        out.add(record);
                        break;
                    }
                }
            }
            return out;
        } catch (Exception ignored) {
            return null;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    static boolean pushItemsBlocking(String endpoint, List<ClipRecord> records) {
        if (endpoint == null || endpoint.trim().isEmpty() || records == null || records.isEmpty()) {
            return false;
        }

        HttpURLConnection connection = null;
        try {
            byte[] bytes = buildItemsPayload(records).toString().getBytes(StandardCharsets.UTF_8);

            connection = (HttpURLConnection) new URL(urlFor(endpoint, "/android/items")).openConnection();
            connection.setRequestMethod("POST");
            connection.setConnectTimeout(3000);
            connection.setReadTimeout(3000);
            connection.setDoOutput(true);
            connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
            connection.setRequestProperty("Content-Length", Integer.toString(bytes.length));

            try (OutputStream out = connection.getOutputStream()) {
                out.write(bytes);
            }

            int code = connection.getResponseCode();
            return code >= 200 && code < 300;
        } catch (Exception ignored) {
            return false;
        } finally {
            if (connection != null) {
                connection.disconnect();
            }
        }
    }

    private static JSONObject buildItemsPayload(List<ClipRecord> records) throws Exception {
        JSONArray items = new JSONArray();
        for (ClipRecord record : records) {
            JSONObject item = new JSONObject();
            item.put("text", record.text);
            item.put("source", "android:" + record.source);
            items.put(item);
        }

        JSONObject payload = new JSONObject();
        payload.put("source", "android");
        payload.put("items", items);
        return payload;
    }

    private static String urlFor(String endpoint, String path) {
        String normalized = endpoint.trim();
        if (normalized.endsWith("/")) {
            normalized = normalized.substring(0, normalized.length() - 1);
        }
        if (normalized.endsWith(path)) {
            return normalized;
        }
        if (normalized.endsWith("/android/items") && path.equals("/android/items/missing")) {
            normalized = normalized.substring(0, normalized.length() - "/android/items".length());
        }
        return normalized + path;
    }

    private static String readAll(InputStream in) throws Exception {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buffer = new byte[4096];
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
        return out.toString("UTF-8");
    }

    static void pushItem(String endpoint, ClipRecord record, Callback callback) {
        if (endpoint == null || endpoint.trim().isEmpty() || record == null) {
            if (callback != null) {
                callback.onComplete(false, "Windows endpoint is empty");
            }
            return;
        }

        String normalized = endpoint.trim();
        if (normalized.endsWith("/")) {
            normalized = normalized.substring(0, normalized.length() - 1);
        }
        final String url = normalized.endsWith("/android/items")
                ? normalized
                : normalized + "/android/items";

        new Thread(() -> {
            HttpURLConnection connection = null;
            try {
                JSONObject item = new JSONObject();
                item.put("text", record.text);
                item.put("source", "android:" + record.source);

                JSONArray items = new JSONArray();
                items.put(item);

                JSONObject payload = new JSONObject();
                payload.put("source", "android");
                payload.put("items", items);

                byte[] bytes = payload.toString().getBytes(StandardCharsets.UTF_8);

                connection = (HttpURLConnection) new URL(url).openConnection();
                connection.setRequestMethod("POST");
                connection.setConnectTimeout(3000);
                connection.setReadTimeout(3000);
                connection.setDoOutput(true);
                connection.setRequestProperty("Content-Type", "application/json; charset=utf-8");
                connection.setRequestProperty("Content-Length", Integer.toString(bytes.length));

                try (OutputStream out = connection.getOutputStream()) {
                    out.write(bytes);
                }

                int code = connection.getResponseCode();
                if (callback != null) {
                    callback.onComplete(code >= 200 && code < 300, "Windows push HTTP " + code);
                }
            } catch (Exception ex) {
                if (callback != null) {
                    callback.onComplete(false, "Windows push failed: " + ex.getClass().getSimpleName());
                }
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }
        }, "ClipboardPpWindowsPush").start();
    }
}
