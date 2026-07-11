package com.clipboardplusplus.androidapi;

import android.content.Context;
import android.content.Intent;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.net.Inet4Address;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;

final class ApiServer {
    static final int PORT = 8765;

    private static ApiServer sInstance;

    private final Context appContext;
    private final ClipboardBridge bridge;
    private volatile boolean running;
    private ServerSocket serverSocket;
    private Thread thread;

    static synchronized ApiServer get(Context context) {
        if (sInstance == null) {
            sInstance = new ApiServer(context.getApplicationContext());
        }
        return sInstance;
    }

    private ApiServer(Context context) {
        appContext = context;
        bridge = ClipboardBridge.get(context);
    }

    synchronized void start() {
        if (running) {
            return;
        }
        running = true;
        thread = new Thread(this::runLoop, "ClipboardPpApiServer");
        thread.setDaemon(true);
        thread.start();
    }

    synchronized void stop() {
        running = false;
        if (serverSocket != null) {
            try {
                serverSocket.close();
            } catch (IOException ignored) {
            }
        }
    }

    boolean isRunning() {
        return running;
    }

    private void runLoop() {
        try (ServerSocket socket = new ServerSocket(PORT)) {
            serverSocket = socket;
            while (running) {
                try {
                    handle(socket.accept());
                } catch (IOException ignored) {
                    if (!running) {
                        return;
                    }
                }
            }
        } catch (IOException ignored) {
            running = false;
        }
    }

    private void handle(Socket socket) {
        try (Socket s = socket;
             BufferedReader reader = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.UTF_8));
             BufferedWriter writer = new BufferedWriter(new OutputStreamWriter(s.getOutputStream(), StandardCharsets.UTF_8))) {

            String requestLine = reader.readLine();
            if (requestLine == null) {
                return;
            }

            String[] request = requestLine.split(" ");
            if (request.length < 2) {
                send(writer, 400, jsonError("Bad request"));
                return;
            }

            int contentLength = 0;
            String line;
            while ((line = reader.readLine()) != null && line.length() > 0) {
                int colon = line.indexOf(':');
                if (colon > 0 && line.substring(0, colon).equalsIgnoreCase("content-length")) {
                    contentLength = Integer.parseInt(line.substring(colon + 1).trim());
                }
            }

            char[] chars = new char[contentLength];
            int read = 0;
            while (read < contentLength) {
                int n = reader.read(chars, read, contentLength - read);
                if (n < 0) {
                    break;
                }
                read += n;
            }

            route(writer, request[0], request[1], new String(chars, 0, read));
        } catch (Exception ignored) {
        }
    }

    private void route(BufferedWriter writer, String method, String path, String body) throws IOException, JSONException {
        if ("GET".equals(method) && "/health".equals(path)) {
            JSONObject obj = new JSONObject();
            obj.put("ok", true);
            obj.put("name", "clipboardpp-android-api");
            obj.put("port", PORT);
            obj.put("status", bridge.getLastStatus());
            send(writer, 200, obj);
            return;
        }

        if ("GET".equals(method) && "/items".equals(path)) {
            JSONObject obj = new JSONObject();
            obj.put("items", bridge.toJsonArray(false));
            obj.put("preserveHistory", bridge.getPreserveHistory());
            send(writer, 200, obj);
            return;
        }

        if ("POST".equals(method) && "/items".equals(path)) {
            JSONObject req = new JSONObject(body);
            boolean makeActive = req.optBoolean("makeActive", true);
            if (req.has("items")) {
                JSONArray array = req.getJSONArray("items");
                String last = null;
                for (int i = 0; i < array.length(); ++i) {
                    JSONObject item = array.getJSONObject(i);
                    last = item.optString("text", "");
                    if (!last.isEmpty()) {
                        bridge.appendText(last, "api");
                    }
                }
                if (makeActive && last != null && !last.isEmpty()) {
                    bridge.setAndroidClipboard(last, false);
                }
            } else {
                String text = req.optString("text", "");
                if (text.isEmpty()) {
                    send(writer, 400, jsonError("text is required"));
                    return;
                }
                bridge.appendText(text, "api");
                if (makeActive) {
                    bridge.setAndroidClipboard(text, false);
                }
            }
            send(writer, 200, ok());
            return;
        }

        if ("POST".equals(method) && "/clipboard/set".equals(path)) {
            JSONObject req = new JSONObject(body);
            String text = req.optString("text", "");
            if (text.isEmpty()) {
                send(writer, 400, jsonError("text is required"));
                return;
            }
            bridge.setAndroidClipboard(text, true);
            send(writer, 200, ok());
            return;
        }

        if ("POST".equals(method) && "/sync/windows".equals(path)) {
            Intent sync = new Intent(appContext, FloatingSyncActivity.class);
            sync.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK |
                    Intent.FLAG_ACTIVITY_MULTIPLE_TASK |
                    Intent.FLAG_ACTIVITY_NO_HISTORY |
                    Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS);
            appContext.startActivity(sync);
            send(writer, 200, ok());
            return;
        }

        if ("POST".equals(method) && "/items/reorder".equals(path)) {
            JSONObject req = new JSONObject(body);
            JSONArray array = req.getJSONArray("ids");
            ArrayList<String> ids = new ArrayList<>();
            for (int i = 0; i < array.length(); ++i) {
                ids.add(array.getString(i));
            }
            bridge.reorder(ids);
            send(writer, 200, ok());
            return;
        }

        if ("POST".equals(method) && path.startsWith("/items/") && path.endsWith("/pin")) {
            String id = path.substring("/items/".length(), path.length() - "/pin".length());
            JSONObject req = new JSONObject(body);
            if (!bridge.setPinned(id, req.optBoolean("pinned", true))) {
                send(writer, 404, jsonError("item not found"));
                return;
            }
            send(writer, 200, ok());
            return;
        }

        if ("POST".equals(method) && path.startsWith("/items/") && path.endsWith("/active")) {
            String id = path.substring("/items/".length(), path.length() - "/active".length());
            if (!bridge.setAndroidClipboardFromItem(id)) {
                send(writer, 404, jsonError("item not found"));
                return;
            }
            send(writer, 200, ok());
            return;
        }

        if ("DELETE".equals(method) && path.startsWith("/items/")) {
            String id = path.substring("/items/".length());
            if (!bridge.remove(id)) {
                send(writer, 404, jsonError("item not found"));
                return;
            }
            send(writer, 200, ok());
            return;
        }

        send(writer, 404, jsonError("not found"));
    }

    private static JSONObject ok() throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("ok", true);
        return obj;
    }

    private static JSONObject jsonError(String message) throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("ok", false);
        obj.put("error", message);
        return obj;
    }

    private static void send(BufferedWriter writer, int status, JSONObject body) throws IOException {
        String text = body.toString();
        writer.write("HTTP/1.1 " + status + " " + reason(status) + "\r\n");
        writer.write("Content-Type: application/json; charset=utf-8\r\n");
        writer.write("Access-Control-Allow-Origin: *\r\n");
        writer.write("Content-Length: " + text.getBytes(StandardCharsets.UTF_8).length + "\r\n");
        writer.write("Connection: close\r\n");
        writer.write("\r\n");
        writer.write(text);
        writer.flush();
    }

    private static String reason(int status) {
        if (status == 200) {
            return "OK";
        }
        if (status == 400) {
            return "Bad Request";
        }
        if (status == 404) {
            return "Not Found";
        }
        return "Error";
    }

    static ArrayList<String> localAddresses() {
        ArrayList<String> out = new ArrayList<>();
        try {
            Enumeration<NetworkInterface> interfaces = NetworkInterface.getNetworkInterfaces();
            for (NetworkInterface nif : Collections.list(interfaces)) {
                Enumeration<java.net.InetAddress> addresses = nif.getInetAddresses();
                for (java.net.InetAddress address : Collections.list(addresses)) {
                    if (!address.isLoopbackAddress() && address instanceof Inet4Address) {
                        out.add("http://" + address.getHostAddress() + ":" + PORT);
                    }
                }
            }
        } catch (Exception ignored) {
        }
        return out;
    }
}
