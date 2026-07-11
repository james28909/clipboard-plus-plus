# Clipboard++ Android API

Experimental Android companion app for Clipboard++.

The app provides the Android side of the Clipboard++ Android clipboard bridge. It uses a lightweight IME path to read Android clipboard text when Android allows access, keeps a local captured-item list, exposes a small HTTP API, and can push captured text to the Windows Clipboard++ sync server.

## Build

Open this folder in Android Studio:

```text
android/clipboardpp-android-api
```

Then build and install the `app` module.

This repository does not currently include a Gradle wrapper. Android Studio can sync the project and use its bundled Gradle/JDK.

## First Test

1. Install the app on Android.
2. Open **Clipboard++ Android API**.
3. Tap **Enable Keyboard** and enable **Clipboard++ Capture Keyboard**.
4. Tap **Pick Keyboard** and select **Clipboard++ Capture Keyboard**.
5. Copy text in another Android app.
6. Tap the floating sync button, or return to the app and tap **Sync current clipboard now**.
7. Check whether the captured item appears in the app list and in the Clipboard++ Android popup list on Windows.

## Push Captures to Windows

Run Clipboard++ on Windows, then enter the Windows endpoint in the Android app:

```text
http://WINDOWS-PC-IP:8766
```

The app posts captured Android items to:

```text
POST http://WINDOWS-PC-IP:8766/android/items
```

Clipboard++ stores received text in its dedicated Android popup list. The Android app tracks pushed items and can ask Windows which captured items are missing before pushing, so a Windows restart or cleared Android list can be repaired by syncing again.

## Sync from Windows

In Clipboard++ on Windows:

1. Open **Settings -> Android**.
2. Enter the Android app endpoint, for example:

```text
http://ANDROID-PHONE-IP:8765
```

3. Save and test the endpoint.
4. Open the popup Android list and click **Sync** to ask the Android app to capture its current clipboard and push missing items to Windows.

You can also right-click text items in any Clipboard++ profile and choose **Send to Android clipboard**.

The default global hotkey for sending highlighted Windows text to Android is:

```text
Ctrl+Alt+Shift+Z
```

## Local API

The app starts a plain HTTP API on port `8765` while the process is alive.

Endpoints:

```text
GET  /health
GET  /items
POST /items
POST /clipboard/set
POST /items/reorder
POST /items/{id}/pin
POST /items/{id}/active
DELETE /items/{id}
POST /sync/windows
```

This is intentionally unauthenticated for the first local proof of concept. Pairing, encryption, and persistent background service behavior should be added before any broader use.
