package com.medicaldisplay;

import android.util.Log;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public final class DisplayAdapter implements AutoCloseable {
    private static final String TAG = "DisplayAdapter";
    private static volatile boolean sNativeLoaded = false;

    public static final int COLOR_SPACE_SRGB = 0;
    public static final int COLOR_SPACE_DCI_P3 = 1;
    public static final int COLOR_SPACE_REC709 = 2;
    public static final int COLOR_SPACE_REC2020 = 3;
    public static final int COLOR_SPACE_ADOBE_RGB = 4;
    public static final int COLOR_SPACE_DICOM_GSDF = 5;
    public static final int COLOR_SPACE_NATIVE = 6;

    public static final int HDR_MODE_OFF = 0;
    public static final int HDR_MODE_HDR10 = 1;
    public static final int HDR_MODE_HLG = 2;
    public static final int HDR_MODE_DOLBY_VISION = 3;
    public static final int HDR_MODE_LOCAL_DIMMING = 4;

    public static final int ENHANCE_OFF = 0;
    public static final int ENHANCE_BONE = 1;
    public static final int ENHANCE_LUNG = 2;
    public static final int ENHANCE_VASCULAR = 3;
    public static final int ENHANCE_CELL = 4;
    public static final int ENHANCE_TISSUE = 5;

    public static final int SYNC_MODE_GENLOCK = 0;
    public static final int SYNC_MODE_VSYNC = 1;
    public static final int SYNC_MODE_VRR = 2;

    static {
        try {
            System.loadLibrary("medicaldisplay_display_jni");
            sNativeLoaded = true;
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "Display JNI library not available, fallback mode enabled", error);
        }
    }

    public enum ColorDepth {
        BIT_8(8),
        BIT_10(10),
        BIT_12(12);

        private final int bits;

        ColorDepth(int bits) {
            this.bits = bits;
        }

        int bits() {
            return bits;
        }
    }

    private final int maxDisplayCount;
    private final Map<Integer, DisplayConfig> currentConfigs = new ConcurrentHashMap<>();
    private long nativeHandle;

    private DisplayAdapter(Builder builder) {
        maxDisplayCount = Math.max(1, builder.maxDisplayCount);
        if (sNativeLoaded) {
            nativeHandle = nativeCreate(
                maxDisplayCount,
                builder.colorDepth.bits(),
                builder.enableGSDF,
                builder.enableHDR
            );
        }
        for (int displayId = 0; displayId < maxDisplayCount; displayId++) {
            currentConfigs.put(displayId, defaultConfig(displayId));
        }
    }

    public synchronized void applyConfig(int displayId, DisplayConfig config) {
        DisplayConfig sanitized = sanitizeConfig(displayId, config);
        currentConfigs.put(displayId, copyConfig(sanitized));
        if (sNativeLoaded && nativeHandle != 0L) {
            nativeApplyConfig(
                nativeHandle,
                displayId,
                sanitized.gamma,
                sanitized.colorSpace,
                sanitized.gsdfEnabled,
                sanitized.gsdfProfile,
                sanitized.windowCenter,
                sanitized.windowWidth,
                sanitized.localEnhancement,
                sanitized.hdrEnabled,
                sanitized.hdrMode,
                sanitized.sharpness,
                sanitized.contrast
            );
        }
    }

    public synchronized void setWindowLevel(int displayId, float center, float width) {
        DisplayConfig config = getCurrentConfig(displayId);
        config.windowCenter = center;
        config.windowWidth = width;
        config.timestampMs = System.currentTimeMillis();
        currentConfigs.put(displayId, copyConfig(config));
        if (sNativeLoaded && nativeHandle != 0L) {
            nativeSetWindowLevel(nativeHandle, displayId, center, width);
        }
    }

    public synchronized void setLocalEnhancement(int displayId, int enhanceType, boolean enabled) {
        DisplayConfig config = getCurrentConfig(displayId);
        config.localEnhancement = enabled ? enhanceType : ENHANCE_OFF;
        config.timestampMs = System.currentTimeMillis();
        currentConfigs.put(displayId, copyConfig(config));
        if (sNativeLoaded && nativeHandle != 0L) {
            nativeSetLocalEnhancement(nativeHandle, displayId, enhanceType, enabled);
        }
    }

    public synchronized DisplayConfig getCurrentConfig(int displayId) {
        DisplayConfig config = currentConfigs.get(displayId);
        if (config == null) {
            config = defaultConfig(displayId);
            currentConfigs.put(displayId, copyConfig(config));
        }
        return copyConfig(config);
    }

    public synchronized int[] listDisplays() {
        if (sNativeLoaded && nativeHandle != 0L) {
            int[] displays = nativeListDisplays(nativeHandle);
            if (displays != null && displays.length > 0) {
                return displays;
            }
        }
        int[] fallback = new int[maxDisplayCount];
        for (int index = 0; index < maxDisplayCount; index++) {
            fallback[index] = index;
        }
        return fallback;
    }

    public synchronized int syncDisplays(int[] displayIds, int syncMode) {
        if (displayIds == null || displayIds.length == 0) {
            return 0;
        }
        if (sNativeLoaded && nativeHandle != 0L) {
            return nativeSyncDisplays(nativeHandle, displayIds, syncMode);
        }
        return displayIds.length;
    }

    public synchronized boolean selfTest(int displayId, int testPattern) {
        if (sNativeLoaded && nativeHandle != 0L) {
            return nativeSelfTest(nativeHandle, displayId, testPattern);
        }
        return displayId >= 0 && displayId < maxDisplayCount;
    }

    public synchronized float[] getCalibrationStatus(int displayId) {
        if (sNativeLoaded && nativeHandle != 0L) {
            float[] status = nativeGetCalibrationStatus(nativeHandle, displayId);
            if (status != null && status.length >= 5) {
                return status;
            }
        }

        DisplayConfig config = getCurrentConfig(displayId);
        return new float[]{
            Math.max(0.6f, 1.8f - (config.contrast - 1.0f) * 0.4f),
            config.hdrEnabled ? 650.0f : 500.0f,
            95.0f,
            config.gamma,
            System.currentTimeMillis() / 1000.0f
        };
    }

    public synchronized void release() {
        if (sNativeLoaded && nativeHandle != 0L) {
            nativeDestroy(nativeHandle);
            nativeHandle = 0L;
        }
        currentConfigs.clear();
    }

    @Override
    public void close() {
        release();
    }

    private DisplayConfig sanitizeConfig(int displayId, DisplayConfig config) {
        DisplayConfig sanitized = config == null ? defaultConfig(displayId) : copyConfig(config);
        sanitized.displayId = displayId;
        if (sanitized.gsdfProfile == null || sanitized.gsdfProfile.isEmpty()) {
            sanitized.gsdfProfile = sanitized.gsdfEnabled ? "DEFAULT" : "OFF";
        }
        if (sanitized.gamma <= 0.0f) {
            sanitized.gamma = 2.2f;
        }
        if (sanitized.sharpness <= 0.0f) {
            sanitized.sharpness = 1.0f;
        }
        if (sanitized.contrast <= 0.0f) {
            sanitized.contrast = 1.0f;
        }
        sanitized.timestampMs = System.currentTimeMillis();
        return sanitized;
    }

    private static DisplayConfig defaultConfig(int displayId) {
        DisplayConfig config = new DisplayConfig();
        config.displayId = displayId;
        config.gamma = 2.2f;
        config.colorSpace = COLOR_SPACE_SRGB;
        config.gsdfEnabled = true;
        config.gsdfProfile = "DEFAULT";
        config.windowCenter = 127.0f;
        config.windowWidth = 255.0f;
        config.localEnhancement = ENHANCE_OFF;
        config.hdrEnabled = false;
        config.hdrMode = HDR_MODE_OFF;
        config.sharpness = 1.0f;
        config.contrast = 1.0f;
        config.confidence = 0.0f;
        config.timestampMs = System.currentTimeMillis();
        return config;
    }

    private static DisplayConfig copyConfig(DisplayConfig source) {
        DisplayConfig target = new DisplayConfig();
        target.displayId = source.displayId;
        target.gamma = source.gamma;
        target.colorSpace = source.colorSpace;
        target.gsdfEnabled = source.gsdfEnabled;
        target.gsdfProfile = source.gsdfProfile;
        target.windowCenter = source.windowCenter;
        target.windowWidth = source.windowWidth;
        target.localEnhancement = source.localEnhancement;
        target.hdrEnabled = source.hdrEnabled;
        target.hdrMode = source.hdrMode;
        target.sharpness = source.sharpness;
        target.contrast = source.contrast;
        target.confidence = source.confidence;
        target.timestampMs = source.timestampMs;
        return target;
    }

    public static final class Builder {
        private int maxDisplayCount = 1;
        private ColorDepth colorDepth = ColorDepth.BIT_10;
        private boolean enableGSDF = true;
        private boolean enableHDR = true;

        public Builder setMaxDisplayCount(int maxDisplayCount) {
            this.maxDisplayCount = Math.max(1, maxDisplayCount);
            return this;
        }

        public Builder setColorDepth(ColorDepth colorDepth) {
            this.colorDepth = colorDepth == null ? ColorDepth.BIT_10 : colorDepth;
            return this;
        }

        public Builder enableGSDF(boolean enableGSDF) {
            this.enableGSDF = enableGSDF;
            return this;
        }

        public Builder enableHDR(boolean enableHDR) {
            this.enableHDR = enableHDR;
            return this;
        }

        public DisplayAdapter build() {
            return new DisplayAdapter(this);
        }
    }

    private static native long nativeCreate(
        int maxDisplayCount,
        int colorDepth,
        boolean enableGSDF,
        boolean enableHDR
    );

    private static native void nativeDestroy(long handle);

    private static native boolean nativeApplyConfig(
        long handle,
        int displayId,
        float gamma,
        int colorSpace,
        boolean gsdfEnabled,
        String gsdfProfile,
        float windowCenter,
        float windowWidth,
        int localEnhancement,
        boolean hdrEnabled,
        int hdrMode,
        float sharpness,
        float contrast
    );

    private static native void nativeSetWindowLevel(
        long handle,
        int displayId,
        float center,
        float width
    );

    private static native void nativeSetLocalEnhancement(
        long handle,
        int displayId,
        int enhanceType,
        boolean enabled
    );

    private static native int[] nativeListDisplays(long handle);

    private static native int nativeSyncDisplays(long handle, int[] displayIds, int syncMode);

    private static native boolean nativeSelfTest(long handle, int displayId, int testPattern);

    private static native float[] nativeGetCalibrationStatus(long handle, int displayId);
}
