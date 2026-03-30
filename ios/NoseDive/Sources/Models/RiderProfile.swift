import Foundation

/// A rider's tune profile — maps to the radar chart axes
struct RiderProfile: Identifiable, Codable {
    var id = UUID()
    var name: String
    var icon: String    // SF Symbol name
    var isBuiltIn: Bool = false
    var createdAt: Date = Date()
    var modifiedAt: Date = Date()

    /// Radar chart axis values (1.0 - 10.0 scale)
    var responsiveness: Double = 5.0
    var stability: Double = 5.0
    var carving: Double = 5.0
    var braking: Double = 5.0
    var safety: Double = 5.0
    var agility: Double = 5.0

    /// Startup/disengage (separate from radar)
    var footpadSensitivity: Double = 5.0
    var disengageSpeed: Double = 5.0

    /// All 6 axis values as an array (for radar chart drawing)
    var axisValues: [Double] {
        [responsiveness, stability, carving, braking, safety, agility]
    }

    static let axisLabels = [
        "Responsiveness", "Stability", "Carving",
        "Braking", "Safety", "Agility"
    ]

    static let axisDescriptions = [
        "How quickly the board reacts to your movements",
        "How planted the board feels at speed",
        "How much the board leans into turns",
        "How aggressively you can decelerate",
        "How early the board warns you about limits",
        "How nimble at low speed and quick to engage"
    ]
}

// MARK: - Built-in Presets

extension RiderProfile {
    static let chill = RiderProfile(
        name: "Chill",
        icon: "water.waves",
        isBuiltIn: true,
        responsiveness: 3, stability: 4, carving: 3,
        braking: 3, safety: 8, agility: 4
    )

    static let flow = RiderProfile(
        name: "Flow",
        icon: "wind",
        isBuiltIn: true,
        responsiveness: 5, stability: 5, carving: 5,
        braking: 5, safety: 6, agility: 5
    )

    static let charge = RiderProfile(
        name: "Charge",
        icon: "bolt.fill",
        isBuiltIn: true,
        responsiveness: 8, stability: 7, carving: 7,
        braking: 7, safety: 3, agility: 7
    )

    static let trail = RiderProfile(
        name: "Trail",
        icon: "leaf.fill",
        isBuiltIn: true,
        responsiveness: 6, stability: 6, carving: 4,
        braking: 5, safety: 6, agility: 8
    )

    static let builtInPresets: [RiderProfile] = [chill, flow, charge, trail]
}
