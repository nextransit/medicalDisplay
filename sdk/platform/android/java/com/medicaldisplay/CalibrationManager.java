package com.medicaldisplay;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class CalibrationManager {
    private final DisplayAdapter displayAdapter;
    private final Map<Integer, float[]> cache = new ConcurrentHashMap<>();

    public CalibrationManager(DisplayAdapter displayAdapter) {
        this.displayAdapter = displayAdapter;
    }

    public float[] getCalibrationStatus(int displayId) {
        float[] status = displayAdapter.getCalibrationStatus(displayId);
        cache.put(displayId, copy(status));
        return status;
    }

    public void applyGsdfProfile(int displayId, String gsdfProfile) {
        DisplayConfig config = displayAdapter.getCurrentConfig(displayId);
        config.gsdfEnabled = true;
        config.gsdfProfile = gsdfProfile == null || gsdfProfile.isEmpty() ? "DEFAULT" : gsdfProfile;
        displayAdapter.applyConfig(displayId, config);
    }

    public void setWindowLevel(int displayId, float center, float width) {
        displayAdapter.setWindowLevel(displayId, center, width);
    }

    public float[] getCachedStatus(int displayId) {
        float[] cached = cache.get(displayId);
        if (cached == null) {
            return getCalibrationStatus(displayId);
        }
        return copy(cached);
    }

    private static float[] copy(float[] source) {
        if (source == null) {
            return new float[]{1.5f, 500.0f, 95.0f, 2.2f, System.currentTimeMillis() / 1000.0f};
        }
        float[] result = new float[source.length];
        System.arraycopy(source, 0, result, 0, source.length);
        return result;
    }
}
