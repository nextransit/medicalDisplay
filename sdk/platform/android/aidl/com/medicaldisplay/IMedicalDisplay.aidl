package com.medicaldisplay;

import com.medicaldisplay.DisplayConfig;
import com.medicaldisplay.IMedicalDisplayCallback;
import com.medicaldisplay.RecognitionResult;

interface IMedicalDisplay {
    RecognitionResult recognize(
        in byte[] imageData,
        int width,
        int height,
        int channels,
        String modalityHint,
        String seriesDescription,
        int bodyPart
    );

    DisplayConfig recognizeAndApply(
        in byte[] imageData,
        int width,
        int height,
        int channels,
        int displayId,
        String modalityHint,
        String seriesDescription,
        int bodyPart
    );

    void applyDisplayConfig(int displayId, in DisplayConfig config);
    void setWindowLevel(int displayId, float center, float width);
    void enableLocalEnhance(int displayId, int enhanceType, boolean enabled);
    DisplayConfig getCurrentDisplayConfig(int displayId);
    int[] listDisplays();
    int syncMultipleDisplays(in int[] displayIds, int syncMode);
    boolean performSelfTest(int displayId, int testPattern);
    float[] getCalibrationStatus(int displayId);
    int getCloudConnectionState();
    void checkForUpdates();
    void registerCallback(IMedicalDisplayCallback callback);
    void unregisterCallback(IMedicalDisplayCallback callback);
}
