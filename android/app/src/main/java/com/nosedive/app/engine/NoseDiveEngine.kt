package com.nosedive.app.engine

import android.content.Context
import android.os.Handler
import android.os.Looper
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

    private val _refloatInfo = MutableStateFlow<RefloatInfo?>(null)
    val refloatInfo: StateFlow<RefloatInfo?> = _refloatInfo.asStateFlow()

    private val _mainFW = MutableStateFlow<FWVersionInfo?>(null)
    val mainFW: StateFlow<FWVersionInfo?> = _mainFW.asStateFlow()

    private val _canDevices = MutableStateFlow<List<Int>>(emptyList())
    val canDevices: StateFlow<List<Int>> = _canDevices.asStateFlow()

    private val _guessedBoardType = MutableStateFlow<String?>(null)
    val guessedBoardType: StateFlow<String?> = _guessedBoardType.asStateFlow()

    private val _refloatInstalling = MutableStateFlow(false)
    val refloatInstalling: StateFlow<Boolean> = _refloatInstalling.asStateFlow()

    private val _refloatInstalled = MutableStateFlow(false)
    val refloatInstalled: StateFlow<Boolean> = _refloatInstalled.asStateFlow()

    private val _activeBoardName = MutableStateFlow<String?>(null)
    val activeBoardName: StateFlow<String?> = _activeBoardName.asStateFlow()

    // BLE service reference for sending data
    var bleService: com.nosedive.app.ble.BLEService? = null

    private val mainHandler = Handler(Looper.getMainLooper())

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
    fun saveBoard() = nativeSaveBoard()

    val hasRefloat: Boolean get() = nativeHasRefloat()
    val speedKmh: Double get() = nativeSpeedKmh()
    val speedMph: Double get() = nativeSpeedMph()
    val boardCount: Int get() = nativeBoardCount()
    val profileCount: Int get() = nativeProfileCount()

    /**
     * Called from C++ via JNI when engine state changes.
     * Refreshes all state flows so Compose UI recomposes.
     * JNI callbacks may arrive on arbitrary threads, so post to main.
     */
    fun onEngineStateChanged() {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            refreshState()
        } else {
            mainHandler.post { refreshState() }
        }
    }

    /**
     * Called from C++ via JNI when engine wants to send a VESC payload.
     * Forwards to the BLE service for transmission.
     */
    fun onEngineSendPayload(data: ByteArray) {
        Log.d(TAG, "Engine send payload: ${data.size} bytes")
        bleService?.send(data)
    }

    private fun refreshState() {
        _hasActiveBoard.value = nativeHasActiveBoard()
        _showWizard.value = nativeShouldShowWizard()

        // Refloat info
        val ri = nativeGetRefloatInfo()
        _refloatInfo.value = if (ri != null && ri.size == 5) {
            RefloatInfo(
                name = ri[0],
                major = ri[1].toIntOrNull() ?: 0,
                minor = ri[2].toIntOrNull() ?: 0,
                patch = ri[3].toIntOrNull() ?: 0,
                suffix = ri[4]
            )
        } else {
            null
        }

        // Refloat install states
        _refloatInstalling.value = nativeRefloatInstalling()
        _refloatInstalled.value = nativeRefloatInstalled()

        // Main firmware info
        val fw = nativeGetMainFW()
        _mainFW.value = if (fw != null && fw.size == 7) {
            FWVersionInfo(
                hwName = fw[0],
                major = fw[1].toIntOrNull() ?: 0,
                minor = fw[2].toIntOrNull() ?: 0,
                uuid = fw[3],
                hwType = fw[4].toIntOrNull() ?: 0,
                customConfigCount = fw[5].toIntOrNull() ?: 0,
                packageName = fw[6]
            )
        } else {
            null
        }

        // CAN devices
        val canCount = nativeCanDeviceCount()
        _canDevices.value = (0 until canCount).map { nativeCanDeviceId(it) }

        // Guessed board type
        _guessedBoardType.value = nativeGuessedBoardType()

        // Active board name
        val ab = nativeGetActiveBoard()
        _activeBoardName.value = if (ab != null && ab.size >= 2) ab[1] else null

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
    private external fun nativeGetRefloatInfo(): Array<String>?
    private external fun nativeGetMainFW(): Array<String>?
    private external fun nativeCanDeviceCount(): Int
    private external fun nativeCanDeviceId(index: Int): Int
    private external fun nativeGuessedBoardType(): String?
    private external fun nativeRefloatInstalling(): Boolean
    private external fun nativeRefloatInstalled(): Boolean
    private external fun nativeGetActiveBoard(): Array<String>?
    private external fun nativeSaveBoard()
    private external fun nativeBoardCount(): Int
    private external fun nativeProfileCount(): Int
}
