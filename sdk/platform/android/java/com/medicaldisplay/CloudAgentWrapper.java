package com.medicaldisplay;

import android.content.Context;
import android.os.SystemClock;
import android.util.Log;

import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.atomic.AtomicInteger;

public final class CloudAgentWrapper implements AutoCloseable {
    private static final String TAG = "CloudAgentWrapper";

    public static final int STATE_DISCONNECTED = 0;
    public static final int STATE_CONNECTING = 1;
    public static final int STATE_CONNECTED = 2;
    public static final int STATE_ERROR = 3;

    public interface StateListener {
        void onStateChanged(int state);
    }

    private final Context appContext;
    private final String serverUrl;
    private final String deviceId;
    private final String firmwareVersion;
    private final ScheduledExecutorService executor;
    private final AtomicInteger state = new AtomicInteger(STATE_DISCONNECTED);
    private volatile StateListener stateListener;

    private CloudAgentWrapper(Builder builder) {
        appContext = builder.context.getApplicationContext();
        serverUrl = builder.serverUrl;
        deviceId = builder.deviceId;
        firmwareVersion = builder.firmwareVersion;
        executor = Executors.newSingleThreadScheduledExecutor();
    }

    public void setStateListener(StateListener listener) {
        stateListener = listener;
    }

    public void connect() {
        if (!state.compareAndSet(STATE_DISCONNECTED, STATE_CONNECTING)) {
            return;
        }
        dispatchState(STATE_CONNECTING);
        executor.execute(() -> {
            SystemClock.sleep(120L);
            Log.i(TAG, "Connected to " + serverUrl + " for device " + deviceId);
            dispatchState(STATE_CONNECTED);
        });
    }

    public void disconnect() {
        dispatchState(STATE_DISCONNECTED);
    }

    public int getConnectionState() {
        return state.get();
    }

    public void checkForUpdates() {
        executor.execute(() -> {
            if (state.get() != STATE_CONNECTED) {
                dispatchState(STATE_CONNECTING);
                SystemClock.sleep(80L);
                dispatchState(STATE_CONNECTED);
            }
            Log.i(
                TAG,
                "Checking OTA/model updates: server=" + serverUrl
                    + ", firmware=" + firmwareVersion
                    + ", package=" + appContext.getPackageName()
            );
        });
    }

    @Override
    public void close() {
        executor.shutdownNow();
        dispatchState(STATE_DISCONNECTED);
    }

    private void dispatchState(int newState) {
        state.set(newState);
        StateListener listener = stateListener;
        if (listener != null) {
            listener.onStateChanged(newState);
        }
    }

    public static final class Builder {
        private final Context context;
        private String serverUrl = "https://ota.medical-display.com";
        private String deviceId = "unknown";
        private String firmwareVersion = "unknown";

        public Builder(Context context) {
            this.context = context;
        }

        public Builder setServerUrl(String serverUrl) {
            this.serverUrl = serverUrl == null || serverUrl.isEmpty()
                ? "https://ota.medical-display.com"
                : serverUrl;
            return this;
        }

        public Builder setDeviceId(String deviceId) {
            this.deviceId = deviceId == null || deviceId.isEmpty() ? "unknown" : deviceId;
            return this;
        }

        public Builder setFirmwareVersion(String firmwareVersion) {
            this.firmwareVersion = firmwareVersion == null || firmwareVersion.isEmpty()
                ? "unknown"
                : firmwareVersion;
            return this;
        }

        public CloudAgentWrapper build() {
            return new CloudAgentWrapper(this);
        }
    }
}
