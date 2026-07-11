# Clipboard++ Android API

Experimental Android companion app for Clipboard++.

This first proof of concept validates whether a tiny Android IME can read clipboard changes while it is the active input method, then exposes a small local HTTP API for Windows integration tests.

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
6. Return to any text field with the Clipboard++ keyboard active.
7. Check whether the captured item appears in the app list.

## Push Captures to Windows

Run Clipboard++ on Windows, then enter the Windows endpoint in the Android app:

```text
http://WINDOWS-PC-IP:8766
```

The app posts captured Android items to:

```text
POST http://WINDOWS-PC-IP:8766/android/items
```

Clipboard++ inserts received text into the active Clipboard++ history with an Android source label.

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
```

This is intentionally unauthenticated for the first local proof of concept. Pairing, encryption, and persistent background service behavior should be added before any broader use.
