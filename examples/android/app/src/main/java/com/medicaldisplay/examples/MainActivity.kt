package com.medicaldisplay.examples

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity
import com.medicaldisplay.examples.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {
    private lateinit var binding: ActivityMainBinding
    private val repository = MedicalDisplayRepository()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnRefresh.setOnClickListener { refreshUi() }
        binding.btnGenerateStudy.setOnClickListener {
            repository.generateSyntheticStudy()
            refreshUi()
        }

        refreshUi()
    }

    private fun refreshUi() {
        val state = repository.currentState()
        binding.txtNativeStatus.text = state.nativeStatus
        binding.txtDicomStatus.text = state.dicomStatus
        binding.txtSyncStatus.text = state.syncStatus
        binding.txtOtaStatus.text = state.otaStatus
        binding.txtLastStudy.text = state.lastStudySummary
    }
}
