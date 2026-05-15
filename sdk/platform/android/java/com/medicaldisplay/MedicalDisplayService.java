package com.medicaldisplay;

import android.app.Service;
import android.content.Intent;
import android.os.Build;
import android.os.IBinder;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.provider.Settings;
import android.util.Log;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MedicalDisplayService extends Service {
    private static final String TAG = "MedicalDisplayService";
    private static final String DEFAULT_MODEL_PATH = "/system/etc/medical_display/model.tflite";

    private final RemoteCallbackList<IMedicalDisplayCallback> callbacks = new RemoteCallbackList<>();
    private final IMedicalDisplay.Stub binder = new IMedicalDisplay.Stub() {
        @Override
        public RecognitionResult recognize(
            byte[] imageData,
            int width,
            int height,
            int channels,
            String modalityHint,
            String seriesDescription,
            int bodyPart
        ) throws RemoteException {
            RecognitionResult result = aiEngine.recognize(
                imageData,
                width,
                height,
                channels,
                modalityHint,
                seriesDescription,
                bodyPart
            );
            notifyRecognitionCompleted(result);
            return result;
        }

        @Override
        public DisplayConfig recognizeAndApply(
            byte[] imageData,
            int width,
            int height,
            int channels,
            int displayId,
            String modalityHint,
            String seriesDescription,
            int bodyPart
        ) throws RemoteException {
            RecognitionResult result = recognize(
                imageData,
                width,
                height,
                channels,
                modalityHint,
                seriesDescription,
                bodyPart
            );
            DisplayConfig config = result.recommendedConfig == null
                ? displayAdapter.getCurrentConfig(displayId)
                : result.recommendedConfig;
            config.displayId = displayId;
            displayAdapter.applyConfig(displayId, config);
            notifyDisplayConfigApplied(displayId, config);
            return config;
        }

        @Override
        public void applyDisplayConfig(int displayId, DisplayConfig config) throws RemoteException {
            displayAdapter.applyConfig(displayId, config);
            notifyDisplayConfigApplied(displayId, displayAdapter.getCurrentConfig(displayId));
        }

        @Override
        public void setWindowLevel(int displayId, float center, float width) throws RemoteException {
            calibrationManager.setWindowLevel(displayId, center, width);
            notifyCalibrationStatus(displayId, calibrationManager.getCalibrationStatus(displayId));
        }

        @Override
        public void enableLocalEnhance(int displayId, int enhanceType, boolean enabled)
            throws RemoteException {
            displayAdapter.setLocalEnhancement(displayId, enhanceType, enabled);
            notifyDisplayConfigApplied(displayId, displayAdapter.getCurrentConfig(displayId));
        }

        @Override
        public DisplayConfig getCurrentDisplayConfig(int displayId) throws RemoteException {
            return displayAdapter.getCurrentConfig(displayId);
        }

        @Override
        public int[] listDisplays() throws RemoteException {
            return displayAdapter.listDisplays();
        }

        @Override
        public int syncMultipleDisplays(int[] displayIds, int syncMode) throws RemoteException {
            return displayAdapter.syncDisplays(displayIds, syncMode);
        }

        @Override
        public boolean performSelfTest(int displayId, int testPattern) throws RemoteException {
            return displayAdapter.selfTest(displayId, testPattern);
        }

        @Override
        public float[] getCalibrationStatus(int displayId) throws RemoteException {
            float[] status = calibrationManager.getCalibrationStatus(displayId);
            notifyCalibrationStatus(displayId, status);
            return status;
        }

        @Override
        public int getCloudConnectionState() throws RemoteException {
            return cloudAgent.getConnectionState();
        }

        @Override
        public void checkForUpdates() throws RemoteException {
            executor.execute(cloudAgent::checkForUpdates);
        }

        @Override
        public void registerCallback(IMedicalDisplayCallback callback) throws RemoteException {
            if (callback != null) {
                callbacks.register(callback);
            }
        }

        @Override
        public void unregisterCallback(IMedicalDisplayCallback callback) throws RemoteException {
            if (callback != null) {
                callbacks.unregister(callback);
            }
        }
    };

    private AIEngineWrapper aiEngine;
    private DisplayAdapter displayAdapter;
    private CalibrationManager calibrationManager;
    private CloudAgentWrapper cloudAgent;
    private ExecutorService executor;

    @Override
    public void onCreate() {
        super.onCreate();
        executor = Executors.newFixedThreadPool(2);

        aiEngine = new AIEngineWrapper.Builder()
            .setRuntime(AIEngineWrapper.Runtime.NNAPI)
            .setAccelerator(AIEngineWrapper.Accelerator.NPU)
            .setModelPath(DEFAULT_MODEL_PATH)
            .setNumThreads(2)
            .build();

        displayAdapter = new DisplayAdapter.Builder()
            .setMaxDisplayCount(4)
            .setColorDepth(DisplayAdapter.ColorDepth.BIT_12)
            .enableGSDF(true)
            .enableHDR(true)
            .build();

        calibrationManager = new CalibrationManager(displayAdapter);
        cloudAgent = new CloudAgentWrapper.Builder(this)
            .setServerUrl("https://ota.medical-display.com")
            .setDeviceId(getDeviceId())
            .setFirmwareVersion(Build.VERSION.RELEASE)
            .build();
        cloudAgent.setStateListener(this::notifyCloudStateChanged);
        cloudAgent.connect();

        Log.i(TAG, "MedicalDisplayService initialized");
    }

    @Override
    public IBinder onBind(Intent intent) {
        return binder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        executor.execute(cloudAgent::checkForUpdates);
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        callbacks.kill();
        if (cloudAgent != null) {
            cloudAgent.close();
        }
        if (aiEngine != null) {
            aiEngine.close();
        }
        if (displayAdapter != null) {
            displayAdapter.close();
        }
        if (executor != null) {
            executor.shutdownNow();
        }
        super.onDestroy();
    }

    private void notifyRecognitionCompleted(RecognitionResult result) {
        int count = callbacks.beginBroadcast();
        try {
            for (int index = 0; index < count; index++) {
                callbacks.getBroadcastItem(index).onRecognitionCompleted(result);
            }
        } catch (RemoteException error) {
            Log.w(TAG, "notifyRecognitionCompleted failed", error);
        } finally {
            callbacks.finishBroadcast();
        }
    }

    private void notifyDisplayConfigApplied(int displayId, DisplayConfig config) {
        int count = callbacks.beginBroadcast();
        try {
            for (int index = 0; index < count; index++) {
                callbacks.getBroadcastItem(index).onDisplayConfigApplied(displayId, config);
            }
        } catch (RemoteException error) {
            Log.w(TAG, "notifyDisplayConfigApplied failed", error);
        } finally {
            callbacks.finishBroadcast();
        }
    }

    private void notifyCalibrationStatus(int displayId, float[] status) {
        int count = callbacks.beginBroadcast();
        try {
            for (int index = 0; index < count; index++) {
                callbacks.getBroadcastItem(index).onCalibrationStatus(displayId, status);
            }
        } catch (RemoteException error) {
            Log.w(TAG, "notifyCalibrationStatus failed", error);
        } finally {
            callbacks.finishBroadcast();
        }
    }

    private void notifyCloudStateChanged(int state) {
        int count = callbacks.beginBroadcast();
        try {
            for (int index = 0; index < count; index++) {
                callbacks.getBroadcastItem(index).onCloudStateChanged(state);
            }
        } catch (RemoteException error) {
            Log.w(TAG, "notifyCloudStateChanged failed", error);
        } finally {
            callbacks.finishBroadcast();
        }
    }

    @SuppressWarnings("unused")
    private void notifyError(int code, String message) {
        int count = callbacks.beginBroadcast();
        try {
            for (int index = 0; index < count; index++) {
                callbacks.getBroadcastItem(index).onError(code, message);
            }
        } catch (RemoteException error) {
            Log.w(TAG, "notifyError failed", error);
        } finally {
            callbacks.finishBroadcast();
        }
    }

    private String getDeviceId() {
        String deviceId = Settings.Secure.getString(
            getContentResolver(),
            Settings.Secure.ANDROID_ID
        );
        return deviceId == null || deviceId.isEmpty() ? "unknown" : deviceId;
    }
}
