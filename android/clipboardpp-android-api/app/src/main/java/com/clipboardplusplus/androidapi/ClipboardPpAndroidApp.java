package com.clipboardplusplus.androidapi;

import android.app.Application;

public class ClipboardPpAndroidApp extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        ClipboardBridge.get(this).captureCurrentClipboard("app-launch");
        ApiServer.get(this).start();
    }
}
