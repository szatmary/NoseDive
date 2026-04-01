import Foundation
import CNoseDive

// MARK: - NoseDive Engine bridge
//
// The C++ Engine owns all business logic. Swift is a thin GUI shell.
// This file provides the engine singleton and helpers for converting
// between C FFI structs and Swift models.

@MainActor
final class NoseDiveEngine {
    static let shared = NoseDiveEngine()

    let handle: OpaquePointer

    private init() {
        let dir = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
        let path = dir.appendingPathComponent("nosedive_data.json").path
        handle = nd_engine_create(path)!
    }

    deinit {
        nd_engine_destroy(handle)
    }
}

// MARK: - C struct ↔ Swift model conversion

enum NoseDiveBridge {

    // MARK: Board

    static func boardFromC(_ cb: nd_board_t) -> Board {
        var b = Board(id: readFixedChar(cb.id), name: readFixedChar(cb.name))
        let bleName = readFixedChar(cb.ble_name)
        b.bleName = bleName.isEmpty ? nil : bleName
        let bleAddr = readFixedChar(cb.ble_address)
        b.bleAddress = bleAddr.isEmpty ? nil : bleAddr
        b.lastConnected = cb.last_connected > 0 ? Date(timeIntervalSince1970: Double(cb.last_connected)) : nil
        b.wizardComplete = cb.wizard_complete
        let hwName = readFixedChar(cb.hw_name)
        b.hwName = hwName.isEmpty ? nil : hwName
        b.fwMajor = cb.fw_major
        b.fwMinor = cb.fw_minor
        let rv = readFixedChar(cb.refloat_version)
        b.refloatVersion = rv.isEmpty ? nil : rv
        b.motorPolePairs = Int(cb.motor_pole_pairs)
        b.wheelCircumferenceM = cb.wheel_circumference_m
        b.batterySeriesCells = Int(cb.battery_series_cells)
        b.batteryVoltageMin = cb.battery_voltage_min
        b.batteryVoltageMax = cb.battery_voltage_max
        b.lifetimeDistanceM = cb.lifetime_distance_m
        b.rideCount = Int(cb.ride_count)
        let pid = readFixedChar(cb.active_profile_id)
        b.activeProfileId = UUID(uuidString: pid)
        return b
    }

    static func boardToC(_ b: Board) -> nd_board_t {
        var cb = nd_board_t()
        copyToFixedChar(&cb.id, b.id)
        copyToFixedChar(&cb.name, b.name)
        copyToFixedChar(&cb.ble_name, b.bleName ?? "")
        copyToFixedChar(&cb.ble_address, b.bleAddress ?? "")
        cb.last_connected = Int64(b.lastConnected?.timeIntervalSince1970 ?? 0)
        cb.wizard_complete = b.wizardComplete
        copyToFixedChar(&cb.hw_name, b.hwName ?? "")
        cb.fw_major = b.fwMajor
        cb.fw_minor = b.fwMinor
        copyToFixedChar(&cb.refloat_version, b.refloatVersion ?? "")
        cb.motor_pole_pairs = Int32(b.motorPolePairs)
        cb.wheel_circumference_m = b.wheelCircumferenceM
        cb.battery_series_cells = Int32(b.batterySeriesCells)
        cb.battery_voltage_min = b.batteryVoltageMin
        cb.battery_voltage_max = b.batteryVoltageMax
        cb.lifetime_distance_m = b.lifetimeDistanceM
        cb.ride_count = Int32(b.rideCount)
        copyToFixedChar(&cb.active_profile_id, b.activeProfileId?.uuidString ?? "")
        return cb
    }

    // MARK: RiderProfile

    static func profileFromC(_ cp: nd_rider_profile_t) -> RiderProfile {
        var p = RiderProfile(name: readFixedChar(cp.name), icon: readFixedChar(cp.icon))
        if let uuid = UUID(uuidString: readFixedChar(cp.id)) { p.id = uuid }
        p.isBuiltIn = cp.is_built_in
        p.createdAt = Date(timeIntervalSince1970: Double(cp.created_at))
        p.modifiedAt = Date(timeIntervalSince1970: Double(cp.modified_at))
        p.responsiveness = cp.responsiveness
        p.stability = cp.stability
        p.carving = cp.carving
        p.braking = cp.braking
        p.safety = cp.safety
        p.agility = cp.agility
        p.footpadSensitivity = cp.footpad_sensitivity
        p.disengageSpeed = cp.disengage_speed
        return p
    }

    static func profileToC(_ p: RiderProfile) -> nd_rider_profile_t {
        var cp = nd_rider_profile_t()
        copyToFixedChar(&cp.id, p.id.uuidString)
        copyToFixedChar(&cp.name, p.name)
        copyToFixedChar(&cp.icon, p.icon)
        cp.is_built_in = p.isBuiltIn
        cp.created_at = Int64(p.createdAt.timeIntervalSince1970)
        cp.modified_at = Int64(p.modifiedAt.timeIntervalSince1970)
        cp.responsiveness = p.responsiveness
        cp.stability = p.stability
        cp.carving = p.carving
        cp.braking = p.braking
        cp.safety = p.safety
        cp.agility = p.agility
        cp.footpad_sensitivity = p.footpadSensitivity
        cp.disengage_speed = p.disengageSpeed
        return cp
    }

    // MARK: Telemetry

    static func telemetryFromC(_ ct: nd_telemetry_t) -> BoardTelemetry {
        var t = BoardTelemetry()
        t.mosfetTemp = ct.temp_mosfet
        t.motorTemp = ct.temp_motor
        t.motorCurrent = ct.motor_current
        t.batteryCurrent = ct.battery_current
        t.dutyCycle = ct.duty_cycle
        t.erpm = ct.erpm
        t.batteryVoltage = ct.battery_voltage
        t.batteryPercent = ct.battery_percent
        t.speed = ct.speed
        t.power = ct.power
        t.tachometer = ct.tachometer
        t.tachometerAbs = ct.tachometer_abs
        t.fault = ct.fault
        return t
    }

    // MARK: Fixed-size char helpers

    static func copyToFixedChar<T>(_ tuple: inout T, _ str: String) {
        withUnsafeMutableBytes(of: &tuple) { buf in
            let bytes = Array(str.utf8)
            let n = min(bytes.count, buf.count - 1)
            buf.baseAddress!.assumingMemoryBound(to: UInt8.self).initialize(repeating: 0, count: buf.count)
            for i in 0..<n {
                buf[i] = bytes[i]
            }
        }
    }

    static func readFixedChar<T>(_ tuple: T) -> String {
        withUnsafeBytes(of: tuple) { buf in
            let ptr = buf.baseAddress!.assumingMemoryBound(to: CChar.self)
            return String(cString: ptr)
        }
    }
}
