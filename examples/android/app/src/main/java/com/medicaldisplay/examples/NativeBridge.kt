package com.medicaldisplay.examples

object NativeBridge {
    init {
        System.loadLibrary("medicaldisplay_demo")
    }

    external fun nativeStatus(): String
    external fun dicomReceiverStatus(): String
    external fun multiDisplayStatus(): String
    external fun cloudDemoStatus(): String
    external fun lastSyntheticStudy(): String
    external fun generateSyntheticStudy()
}
