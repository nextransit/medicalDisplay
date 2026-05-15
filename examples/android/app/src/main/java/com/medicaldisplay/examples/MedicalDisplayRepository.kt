package com.medicaldisplay.examples

data class DemoUiState(
    val nativeStatus: String,
    val dicomStatus: String,
    val syncStatus: String,
    val otaStatus: String,
    val lastStudySummary: String
)

class MedicalDisplayRepository {
    fun currentState(): DemoUiState {
        return DemoUiState(
            nativeStatus = NativeBridge.nativeStatus(),
            dicomStatus = NativeBridge.dicomReceiverStatus(),
            syncStatus = NativeBridge.multiDisplayStatus(),
            otaStatus = NativeBridge.cloudDemoStatus(),
            lastStudySummary = NativeBridge.lastSyntheticStudy()
        )
    }

    fun generateSyntheticStudy() {
        NativeBridge.generateSyntheticStudy()
    }
}
