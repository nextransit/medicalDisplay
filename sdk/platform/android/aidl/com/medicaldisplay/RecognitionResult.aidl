package com.medicaldisplay;

import com.medicaldisplay.DisplayConfig;

parcelable RecognitionResult {
    int modality;
    String modalityName;
    float confidence;
    float inferenceTimeMs;
    int bodyPart;
    DisplayConfig recommendedConfig;
}
