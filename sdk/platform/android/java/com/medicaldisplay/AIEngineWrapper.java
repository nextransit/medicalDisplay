package com.medicaldisplay;

import android.util.Log;

public final class AIEngineWrapper implements AutoCloseable {
    private static final String TAG = "AIEngineWrapper";
    private static volatile boolean sNativeLoaded = false;

    public static final int MODALITY_UNKNOWN = 0;
    public static final int MODALITY_CT = 1;
    public static final int MODALITY_MR = 2;
    public static final int MODALITY_DX = 3;
    public static final int MODALITY_CR = 4;
    public static final int MODALITY_US = 5;
    public static final int MODALITY_ES = 6;
    public static final int MODALITY_SM = 7;
    public static final int MODALITY_PT = 8;
    public static final int MODALITY_XA = 9;
    public static final int MODALITY_RF = 10;
    public static final int MODALITY_OP = 11;
    public static final int MODALITY_SURGICAL = 12;

    static {
        try {
            System.loadLibrary("medicaldisplay_ai_jni");
            sNativeLoaded = true;
        } catch (UnsatisfiedLinkError error) {
            Log.w(TAG, "AI JNI library not available, fallback mode enabled", error);
        }
    }

    public enum Runtime {
        CPU,
        NNAPI,
        GPU
    }

    public enum Accelerator {
        CPU,
        GPU,
        NPU
    }

    private final Runtime runtime;
    private final Accelerator accelerator;
    private final String modelPath;
    private final int numThreads;
    private final float scoreThreshold;
    private long nativeHandle;

    private AIEngineWrapper(Builder builder) {
        runtime = builder.runtime;
        accelerator = builder.accelerator;
        modelPath = builder.modelPath;
        numThreads = builder.numThreads;
        scoreThreshold = builder.scoreThreshold;
        if (sNativeLoaded) {
            nativeHandle = nativeCreate(
                modelPath,
                accelerator == Accelerator.NPU || runtime == Runtime.NNAPI,
                numThreads,
                scoreThreshold
            );
        }
    }

    public synchronized RecognitionResult recognize(
        byte[] imageData,
        int width,
        int height,
        int channels,
        String modalityHint,
        String seriesDescription,
        int bodyPart
    ) {
        if (imageData == null || imageData.length == 0 || width <= 0 || height <= 0) {
            return createFallbackResult(modalityHint, seriesDescription, bodyPart);
        }

        float[] raw = null;
        if (sNativeLoaded && nativeHandle != 0L) {
            raw = nativeRecognize(
                nativeHandle,
                imageData,
                width,
                height,
                channels,
                modalityHint,
                seriesDescription,
                bodyPart
            );
        }

        if (raw == null || raw.length < 14) {
            return createFallbackResult(modalityHint, seriesDescription, bodyPart);
        }

        RecognitionResult result = new RecognitionResult();
        result.modality = Math.round(raw[0]);
        result.modalityName = modalityToName(result.modality);
        result.confidence = raw[1];
        result.inferenceTimeMs = raw[2];
        result.bodyPart = Math.round(raw[3]);
        result.recommendedConfig = toDisplayConfig(result.modality, raw, 0);
        return result;
    }

    public synchronized float[] getWindowRecommendations(int modality) {
        if (sNativeLoaded && nativeHandle != 0L) {
            float[] raw = nativeGetWindowRecommendations(nativeHandle, modality);
            if (raw != null && raw.length > 0) {
                return raw;
            }
        }
        return defaultWindowRecommendations(modality);
    }

    public static RecognitionResult createFallbackResult(
        String modalityHint,
        String seriesDescription,
        int bodyPart
    ) {
        RecognitionResult result = new RecognitionResult();
        result.modality = modalityFromHints(modalityHint, seriesDescription);
        result.modalityName = modalityToName(result.modality);
        result.confidence = 0.55f;
        result.inferenceTimeMs = 0.0f;
        result.bodyPart = bodyPart;
        float[] defaults = defaultRawStrategy(result.modality);
        result.recommendedConfig = toDisplayConfig(result.modality, defaults, 0);
        return result;
    }

    public synchronized void release() {
        if (sNativeLoaded && nativeHandle != 0L) {
            nativeDestroy(nativeHandle);
            nativeHandle = 0L;
        }
    }

    @Override
    public void close() {
        release();
    }

    public static int modalityFromHints(String modalityHint, String seriesDescription) {
        int fromModality = modalityFromToken(modalityHint);
        if (fromModality != MODALITY_UNKNOWN) {
            return fromModality;
        }
        return modalityFromToken(seriesDescription);
    }

    public static String modalityToName(int modality) {
        switch (modality) {
            case MODALITY_CT:
                return "CT";
            case MODALITY_MR:
                return "MR";
            case MODALITY_DX:
                return "DX";
            case MODALITY_CR:
                return "CR";
            case MODALITY_US:
                return "US";
            case MODALITY_ES:
                return "ES";
            case MODALITY_SM:
                return "SM";
            case MODALITY_PT:
                return "PT";
            case MODALITY_XA:
                return "XA";
            case MODALITY_RF:
                return "RF";
            case MODALITY_OP:
                return "OP";
            case MODALITY_SURGICAL:
                return "SURGICAL";
            default:
                return "UNKNOWN";
        }
    }

    private static int modalityFromToken(String token) {
        if (token == null) {
            return MODALITY_UNKNOWN;
        }
        String normalized = token.trim().toLowerCase();
        if (normalized.isEmpty()) {
            return MODALITY_UNKNOWN;
        }
        if (normalized.contains("ct")) {
            return MODALITY_CT;
        }
        if (normalized.contains("mr") || normalized.contains("mri")) {
            return MODALITY_MR;
        }
        if (normalized.contains("dx") || normalized.contains("dr")) {
            return MODALITY_DX;
        }
        if (normalized.contains("cr")) {
            return MODALITY_CR;
        }
        if (normalized.contains("us") || normalized.contains("ultra")) {
            return MODALITY_US;
        }
        if (normalized.contains("endo") || normalized.contains("scope")) {
            return MODALITY_ES;
        }
        if (normalized.contains("path") || normalized.contains("slide") || normalized.contains("wsi")) {
            return MODALITY_SM;
        }
        if (normalized.contains("pet")) {
            return MODALITY_PT;
        }
        if (normalized.contains("xa") || normalized.contains("angi")) {
            return MODALITY_XA;
        }
        if (normalized.contains("rf") || normalized.contains("fluoro")) {
            return MODALITY_RF;
        }
        if (normalized.contains("oph") || normalized.contains("retina")) {
            return MODALITY_OP;
        }
        if (normalized.contains("surgical") || normalized.contains("or ")) {
            return MODALITY_SURGICAL;
        }
        return MODALITY_UNKNOWN;
    }

    private static DisplayConfig toDisplayConfig(int modality, float[] raw, int displayId) {
        DisplayConfig config = new DisplayConfig();
        config.displayId = displayId;
        config.gamma = raw[4];
        config.colorSpace = Math.round(raw[5]);
        config.gsdfEnabled = raw[6] > 0.5f;
        config.gsdfProfile = defaultGsdfProfile(modality);
        config.windowCenter = raw[7];
        config.windowWidth = raw[8];
        config.localEnhancement = Math.round(raw[9]);
        config.sharpness = raw[10];
        config.contrast = raw[11];
        config.hdrEnabled = raw[12] > 0.5f;
        config.hdrMode = Math.round(raw[13]);
        config.confidence = raw[1];
        config.timestampMs = System.currentTimeMillis();
        return config;
    }

    private static String defaultGsdfProfile(int modality) {
        switch (modality) {
            case MODALITY_CT:
                return "CT_STANDARD";
            case MODALITY_MR:
                return "MR_STANDARD";
            case MODALITY_US:
                return "US_STANDARD";
            case MODALITY_ES:
            case MODALITY_SURGICAL:
                return "SURGICAL";
            case MODALITY_SM:
                return "PATHOLOGY";
            default:
                return "DEFAULT";
        }
    }

    private static float[] defaultRawStrategy(int modality) {
        float[] values = new float[14];
        values[0] = modality;
        values[1] = 0.55f;
        values[2] = 0.0f;
        values[3] = 0.0f;
        values[4] = 2.2f;
        values[5] = DisplayAdapter.COLOR_SPACE_SRGB;
        values[6] = 0.0f;
        values[7] = 127.0f;
        values[8] = 255.0f;
        values[9] = DisplayAdapter.ENHANCE_OFF;
        values[10] = 1.0f;
        values[11] = 1.0f;
        values[12] = 0.0f;
        values[13] = DisplayAdapter.HDR_MODE_OFF;

        switch (modality) {
            case MODALITY_CT:
                values[4] = 2.0f;
                values[5] = DisplayAdapter.COLOR_SPACE_DICOM_GSDF;
                values[6] = 1.0f;
                values[7] = 40.0f;
                values[8] = 400.0f;
                values[9] = DisplayAdapter.ENHANCE_BONE;
                values[10] = 1.2f;
                values[11] = 1.1f;
                break;
            case MODALITY_MR:
                values[5] = DisplayAdapter.COLOR_SPACE_DICOM_GSDF;
                values[6] = 1.0f;
                values[7] = 500.0f;
                values[8] = 1000.0f;
                values[9] = DisplayAdapter.ENHANCE_LUNG;
                break;
            case MODALITY_US:
                values[9] = DisplayAdapter.ENHANCE_VASCULAR;
                values[11] = 1.15f;
                break;
            case MODALITY_ES:
            case MODALITY_SURGICAL:
                values[4] = 2.4f;
                values[5] = DisplayAdapter.COLOR_SPACE_DCI_P3;
                values[9] = DisplayAdapter.ENHANCE_TISSUE;
                values[12] = 1.0f;
                values[13] = DisplayAdapter.HDR_MODE_HLG;
                break;
            case MODALITY_SM:
                values[9] = DisplayAdapter.ENHANCE_CELL;
                values[10] = 1.3f;
                break;
            default:
                break;
        }
        return values;
    }

    private static float[] defaultWindowRecommendations(int modality) {
        switch (modality) {
            case MODALITY_CT:
                return new float[]{40.0f, 400.0f, -600.0f, 1500.0f, 300.0f, 2000.0f};
            case MODALITY_MR:
                return new float[]{500.0f, 1000.0f};
            case MODALITY_US:
                return new float[]{127.0f, 255.0f};
            default:
                return new float[]{127.0f, 255.0f};
        }
    }

    public static final class Builder {
        private Runtime runtime = Runtime.NNAPI;
        private Accelerator accelerator = Accelerator.NPU;
        private String modelPath = "";
        private int numThreads = 2;
        private float scoreThreshold = 0.6f;

        public Builder setRuntime(Runtime runtime) {
            this.runtime = runtime;
            return this;
        }

        public Builder setAccelerator(Accelerator accelerator) {
            this.accelerator = accelerator;
            return this;
        }

        public Builder setModelPath(String modelPath) {
            this.modelPath = modelPath == null ? "" : modelPath;
            return this;
        }

        public Builder setNumThreads(int numThreads) {
            this.numThreads = Math.max(1, numThreads);
            return this;
        }

        public Builder setScoreThreshold(float scoreThreshold) {
            this.scoreThreshold = Math.max(0.1f, Math.min(1.0f, scoreThreshold));
            return this;
        }

        public AIEngineWrapper build() {
            return new AIEngineWrapper(this);
        }
    }

    private static native long nativeCreate(
        String modelPath,
        boolean preferNpu,
        int numThreads,
        float scoreThreshold
    );

    private static native void nativeDestroy(long handle);

    private static native float[] nativeRecognize(
        long handle,
        byte[] imageData,
        int width,
        int height,
        int channels,
        String modalityHint,
        String seriesDescription,
        int bodyPart
    );

    private static native float[] nativeGetWindowRecommendations(long handle, int modality);
}
