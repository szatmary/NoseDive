import Foundation

/// A registered board in the user's fleet
struct Board: Identifiable, Codable {
    var id: String      // UUID from COMM_FW_VERSION (hex string)
    var name: String
    var bleName: String?
    var bleAddress: String?
    var lastConnected: Date?
    var wizardComplete: Bool = false

    // Hardware info (from COMM_FW_VERSION)
    var hwName: String?
    var fwMajor: UInt8 = 0
    var fwMinor: UInt8 = 0
    var refloatVersion: String?

    // Board profile
    var motorPolePairs: Int = 15
    var wheelCircumferenceM: Double = 0.8778
    var batterySeriesCells: Int = 20
    var batteryVoltageMin: Double = 60.0
    var batteryVoltageMax: Double = 84.0

    // Stats
    var lifetimeDistanceM: Double = 0
    var rideCount: Int = 0

    // Active rider profile
    var activeProfileId: UUID?

    /// Convert ERPM to speed in m/s
    func speedFromERPM(_ erpm: Double) -> Double {
        guard wheelCircumferenceM > 0, motorPolePairs > 0 else { return 0 }
        let erpmPerMPS = Double(motorPolePairs) * 60.0 / wheelCircumferenceM
        return erpm / erpmPerMPS
    }

    /// Estimate battery percentage from voltage
    func batteryPercent(voltage: Double) -> Double {
        guard batteryVoltageMax > batteryVoltageMin else { return 0 }
        let pct = (voltage - batteryVoltageMin) / (batteryVoltageMax - batteryVoltageMin) * 100
        return min(100, max(0, pct))
    }
}
