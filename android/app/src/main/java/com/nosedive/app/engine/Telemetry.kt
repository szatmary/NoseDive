package com.nosedive.app.engine

/**
 * Real-time telemetry data from the C++ engine.
 * Mirrors nd_telemetry_t from ffi.h.
 */
data class Telemetry(
    val tempMosfet: Double = 0.0,
    val tempMotor: Double = 0.0,
    val motorCurrent: Double = 0.0,
    val batteryCurrent: Double = 0.0,
    val dutyCycle: Double = 0.0,
    val erpm: Double = 0.0,
    val batteryVoltage: Double = 0.0,
    val batteryPercent: Double = 0.0,
    val speed: Double = 0.0,
    val power: Double = 0.0,
    val tachometer: Int = 0,
    val tachometerAbs: Int = 0,
    val fault: Int = 0
)
