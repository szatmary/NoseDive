package com.nosedive.app.engine

import android.content.Context
import android.util.Log
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.io.File

/**
 * Kotlin wrapper around the C++ NoseDive engine via JNI.
 * All business logic lives in C++. This is a thin bridge.
 *
 * The engine is app-scoped and lives for the process lifetime.
 */
class NoseDiveEngine private constructor(context: Context) {

    companion object {
        private const val TAG = "NoseDiveEngine"

        init {
            System.loadLibrary("nosedive_jni")
        }

        @Volatile
        private var instance: NoseDiveEngine? = null

        fun getInstance(context: Context): NoseDiveEngine {
            return instance ?: synchronized(this) {
                instance ?: NoseDiveEngine(context.applicationContext).also { instance = it }
            }
        }
    }

    // State flows for Compose UI
    private val _telemetry = MutableStateFlow(Telemetry())
    val telemetry: StateFlow<Telemetry> = _telemetry.asStateFlow()

    private val _hasActiveBoard = MutableStateFlow(false)
    val hasActiveBoard: StateFlow<Boolean> = _hasActiveBoard.asStateFlow()

    private val _showWizard = MutableStateFlow(false)
    val showWizard: StateFlow<Boolean> = _showWizard.asStateFlow()

    // Pending send payload — BLE service reads this to transmit
    private val _pendingSendPayload = MutableStateFlow<ByteArray?>(null)
    val pendingSendPayload: StateFlow<ByteArray?> = _pendingSendPayload.asStateFlow()

    // BLE service reference for sending data
    var bleService: com.nosedive.app.ble.BLEService? = null

    init {
        val storagePath = File(context.filesDir, "nosedive_data.json").absolutePath
        nativeInit(storagePath)
    }

    fun onConnected() {
        nativeOnConnected()
        // State callback from C++ will trigger refreshState() automatically
    }

    fun onDisconnected() {
        nativeOnDisconnected()
        // State callback from C++ will trigger refreshState() automatically
    }

    fun handlePayload(data: ByteArray) {
        nativeHandlePayload(data)
        // State callback from C++ will trigger refreshState() automatically
    }

    fun installRefloat() = nativeInstallRefloat()
    fun dismissWizard() = nativeDismissWizard()

    val hasRefloat: Boolean get() = nativeHasRefloat()
    val speedKmh: Double get() = nativeSpeedKmh()
    val speedMph: Double get() = nativeSpeedMph()
    val boardCount: Int get() = nativeBoardCount()
    val profileCount: Int get() = nativeProfileCount()

    /**
     * Called from C++ via JNI when engine state changes.
     * Refreshes all state flows so Compose UI recomposes.
     */
    fun onEngineStateChanged() {
        refreshState()
    }

    /**
     * Called from C++ via JNI when engine wants to send a VESC payload.
     * Forwards to the BLE service for transmission.
     */
    fun onEngineSendPayload(data: ByteArray) {
        Log.d(TAG, "Engine send payload: ${data.size} bytes")
        _pendingSendPayload.value = data
        bleService?.send(data)
    }

    private fun refreshState() {
        _hasActiveBoard.value = nativeHasActiveBoard()
        _showWizard.value = nativeShouldShowWizard()

        // Update telemetry from flat array
        val raw = nativeGetTelemetry()
        if (raw.size >= 13) {
            _telemetry.value = Telemetry(
                tempMosfet = raw[0],
                tempMotor = raw[1],
                motorCurrent = raw[2],
                batteryCurrent = raw[3],
                dutyCycle = raw[4],
                erpm = raw[5],
                batteryVoltage = raw[6],
                batteryPercent = raw[7],
                speed = raw[8],
                power = raw[9],
                tachometer = raw[10].toInt(),
                tachometerAbs = raw[11].toInt(),
                fault = raw[12].toInt()
            )
        }
    }

    // --- JNI native methods ---
    private external fun nativeInit(storagePath: String)
    private external fun nativeOnConnected()
    private external fun nativeOnDisconnected()
    private external fun nativeHandlePayload(data: ByteArray)
    private external fun nativeHasActiveBoard(): Boolean
    private external fun nativeHasRefloat(): Boolean
    private external fun nativeInstallRefloat()
    private external fun nativeShouldShowWizard(): Boolean
    private external fun nativeDismissWizard()
    private external fun nativeSpeedKmh(): Double
    private external fun nativeSpeedMph(): Double
    private external fun nativeGetTelemetry(): DoubleArray
    private external fun nativeBoardCount(): Int
    private external fun nativeProfileCount(): Int
}
