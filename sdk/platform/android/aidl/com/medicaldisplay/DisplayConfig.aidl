package com.medicaldisplay;

parcelable DisplayConfig {
    int displayId;
    float gamma;
    int colorSpace;
    boolean gsdfEnabled;
    String gsdfProfile;
    float windowCenter;
    float windowWidth;
    int localEnhancement;
    boolean hdrEnabled;
    int hdrMode;
    float sharpness;
    float contrast;
    float confidence;
    long timestampMs;
}
