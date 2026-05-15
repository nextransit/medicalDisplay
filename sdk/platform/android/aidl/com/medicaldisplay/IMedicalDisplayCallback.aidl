package com.medicaldisplay;

import com.medicaldisplay.DisplayConfig;
import com.medicaldisplay.RecognitionResult;

interface IMedicalDisplayCallback {
    void onRecognitionCompleted(in RecognitionResult result);
    void onDisplayConfigApplied(int displayId, in DisplayConfig config);
    void onCalibrationStatus(int displayId, in float[] status);
    void onCloudStateChanged(int state);
    void onError(int code, String message);
}
