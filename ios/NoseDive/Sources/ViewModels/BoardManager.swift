import SwiftUI
import Combine

@MainActor
class BoardManager: ObservableObject {
    // Connection
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isScanning = false
    @Published var discoveredDevices: [DiscoveredDevice] = []

    // Active board
    @Published var activeBoard: Board?
    @Published var telemetry = BoardTelemetry()
    @Published var refloatState = RefloatState()

    // Fleet
    @Published var boards: [Board] = []

    // Profiles
    @Published var riderProfiles: [RiderProfile] = RiderProfile.builtInPresets
    @Published var activeProfile: RiderProfile = .flow

    // BLE
    private var bleService: BLEService?

    enum ConnectionState: Equatable {
        case disconnected
        case scanning
        case connecting(String)
        case connected
    }

    init() {
        bleService = BLEService { [weak self] event in
            Task { @MainActor in
                self?.handleBLEEvent(event)
            }
        }
    }

    // MARK: - Scanning

    func startScan() {
        guard connectionState == .disconnected else { return }
        discoveredDevices = []
        isScanning = true
        connectionState = .scanning
        bleService?.startScan()
    }

    func stopScan() {
        isScanning = false
        if connectionState == .scanning {
            connectionState = .disconnected
        }
        bleService?.stopScan()
    }

    // MARK: - Connection

    func connect(to device: DiscoveredDevice) {
        stopScan()
        connectionState = .connecting(device.name)
        bleService?.connect(to: device.identifier)
    }

    func disconnect() {
        bleService?.disconnect()
        connectionState = .disconnected
        telemetry = BoardTelemetry()
        refloatState = RefloatState()
    }

    // MARK: - Profiles

    func selectProfile(_ profile: RiderProfile) {
        activeProfile = profile
        if var board = activeBoard {
            board.activeProfileId = profile.id
            activeBoard = board
        }
    }

    func addProfile(_ profile: RiderProfile) {
        riderProfiles.append(profile)
    }

    func updateProfile(_ profile: RiderProfile) {
        if let idx = riderProfiles.firstIndex(where: { $0.id == profile.id }) {
            riderProfiles[idx] = profile
        }
        if activeProfile.id == profile.id {
            activeProfile = profile
        }
    }

    // MARK: - BLE Events

    private func handleBLEEvent(_ event: BLEService.Event) {
        switch event {
        case .discovered(let device):
            if !discoveredDevices.contains(where: { $0.identifier == device.identifier }) {
                discoveredDevices.append(device)
            }
        case .connected:
            connectionState = .connected
        case .disconnected:
            connectionState = .disconnected
        case .telemetry(let t):
            telemetry = t
        case .refloat(let r):
            refloatState = r
        }
    }

    // MARK: - Computed

    var isConnected: Bool {
        connectionState == .connected
    }

    var speedKmh: Double {
        telemetry.speed * 3.6
    }

    var speedMph: Double {
        telemetry.speed * 2.237
    }
}
