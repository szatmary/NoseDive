import Foundation
import SwiftUI

/// Real-time telemetry from the board
struct BoardTelemetry {
    var speed: Double = 0           // m/s
    var dutyCycle: Double = 0       // 0-1
    var batteryVoltage: Double = 0  // volts
    var batteryPercent: Double = 0  // 0-100
    var motorCurrent: Double = 0    // amps
    var batteryCurrent: Double = 0  // amps
    var power: Double = 0           // watts
    var mosfetTemp: Double = 0      // celsius
    var motorTemp: Double = 0       // celsius
    var pitch: Double = 0           // degrees
    var roll: Double = 0            // degrees
    var erpm: Double = 0
    var tachometer: Int32 = 0
    var tachometerAbs: Int32 = 0
    var tripDistance: Double = 0    // meters
    var fault: UInt8 = 0
}

/// Refloat package info (from C++ engine via nd_refloat_info_t)
struct RefloatInfo {
    var name: String = ""
    var major: UInt8 = 0
    var minor: UInt8 = 0
    var patch: UInt8 = 0
    var suffix: String = ""

    var versionString: String {
        var s = "\(major).\(minor).\(patch)"
        if !suffix.isEmpty { s += "-\(suffix)" }
        return s
    }
}

/// Firmware version info (from C++ engine via nd_fw_version_t)
struct FWVersionInfo {
    var hwName: String = ""
    var major: UInt8 = 0
    var minor: UInt8 = 0
    var uuid: String = ""
    var hwType: UInt8 = 0
    var customConfigCount: UInt8 = 0
    var packageName: String = ""

    var versionString: String { "\(major).\(minor)" }
}

/// Refloat-specific telemetry
struct RefloatState {
    var runState: RunState = .disabled
    var footpad: FootpadState = .none
    var setpoint: Double = 0
    var atrSetpoint: Double = 0
    var balancePitch: Double = 0
    var balanceCurrent: Double = 0
    var adc1: Double = 0
    var adc2: Double = 0
}

enum RunState: UInt8, CustomStringConvertible {
    case disabled = 0
    case startup = 1
    case ready = 2
    case running = 3

    var description: String {
        switch self {
        case .disabled: return "Disabled"
        case .startup: return "Starting"
        case .ready: return "Ready"
        case .running: return "Riding"
        }
    }

    var color: SwiftUI.Color {
        switch self {
        case .disabled: return Theme.textTertiary
        case .startup: return Theme.warning
        case .ready: return Theme.primary
        case .running: return Theme.success
        }
    }
}

enum FootpadState: UInt8, CustomStringConvertible {
    case none = 0
    case left = 1
    case right = 2
    case both = 3

    var description: String {
        switch self {
        case .none: return "Off"
        case .left: return "Left"
        case .right: return "Right"
        case .both: return "Both"
        }
    }
}
