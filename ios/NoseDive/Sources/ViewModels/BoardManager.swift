import SwiftUI
import Combine

@MainActor
class BoardManager: ObservableObject {
    // Connection
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isScanning = false
    @Published var discoveredDevices: [DiscoveredDevice] = []
    @Published var canDevices: [UInt8] = [] // CAN bus device IDs from PING_CAN

    // Active board
    @Published var activeBoard: Board?
    @Published var telemetry = BoardTelemetry()
    @Published var refloatState = RefloatState()

    // Fleet
    @Published var boards: [Board] = []

    // Profiles
    @Published var riderProfiles: [RiderProfile] = RiderProfile.builtInPresets
    @Published var activeProfile: RiderProfile = .flow

    // Transport
    private var bleService: BLEService?
    private var tcpTransport: TCPTransport?
    private var activeTransport: TransportKind = .none

    enum TransportKind {
        case none, ble, tcp
    }

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

    // MARK: - BLE Scanning

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

    // MARK: - BLE Connection

    func connect(to device: DiscoveredDevice) {
        stopScan()
        connectionState = .connecting(device.name)
        activeTransport = .ble
        bleService?.connect(to: device.identifier)
    }

    // MARK: - TCP Connection

    func connectTCP(host: String, port: UInt16) {
        disconnect()
        connectionState = .connecting("\(host):\(port)")
        activeTransport = .tcp

        let transport = TCPTransport { [weak self] event in
            Task { @MainActor in
                self?.handleTCPEvent(event)
            }
        }
        tcpTransport = transport
        transport.connect(host: host, port: port)
    }

    // MARK: - Disconnect

    func disconnect() {
        switch activeTransport {
        case .ble:
            bleService?.disconnect()
        case .tcp:
            tcpTransport?.stopPolling()
            tcpTransport?.disconnect()
            tcpTransport = nil
        case .none:
            break
        }
        activeTransport = .none
        connectionState = .disconnected
        telemetry = BoardTelemetry()
        refloatState = RefloatState()
        canDevices = []
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
            activeTransport = .none
        case .telemetry(let t):
            telemetry = t
        case .refloat(let r):
            refloatState = r
        }
    }

    // MARK: - TCP Events

    private func handleTCPEvent(_ event: TCPTransport.Event) {
        switch event {
        case .connected:
            connectionState = .connected
            // Immediately request FW version and CAN scan
            tcpTransport?.requestFWVersion()
            tcpTransport?.requestPingCAN()
            // Start polling telemetry at 10 Hz
            tcpTransport?.startPolling(interval: 0.1)

        case .disconnected:
            connectionState = .disconnected
            activeTransport = .none
            tcpTransport = nil

        case .packet(let payload):
            handleVESCPayload(payload)
        }
    }

    // MARK: - Payload dispatch

    private func handleVESCPayload(_ payload: Data) {
        guard !payload.isEmpty else { return }
        let cmd = payload[payload.startIndex]

        switch cmd {
        case VESCPacket.CommPacketID.getValues.rawValue:
            if var t = VESCPacket.parseValues(payload) {
                // Enrich with board-specific calculations
                if let board = activeBoard {
                    t.speed = board.speedFromERPM(t.erpm)
                    t.batteryPercent = board.batteryPercent(voltage: t.batteryVoltage)
                }
                telemetry = t
            }

        case VESCPacket.CommPacketID.fwVersion.rawValue:
            parseFWVersion(payload)

        case VESCPacket.CommPacketID.pingCAN.rawValue:
            canDevices = VESCPacket.parsePingCAN(payload)

        default:
            break
        }
    }

    private func parseFWVersion(_ payload: Data) {
        guard payload.count > 3 else { return }
        let major = payload[payload.startIndex + 1]
        let minor = payload[payload.startIndex + 2]

        // Parse hw name (null-terminated string starting at offset 3)
        var hwName = ""
        var idx = payload.startIndex + 3
        while idx < payload.endIndex && payload[idx] != 0 {
            hwName.append(Character(UnicodeScalar(payload[idx])))
            idx += 1
        }
        idx += 1 // skip null

        // Parse UUID (12 bytes)
        var uuid = ""
        if idx + 12 <= payload.endIndex {
            let uuidBytes = payload[idx..<idx+12]
            uuid = uuidBytes.map { String(format: "%02x", $0) }.joined()
            idx += 12
        }

        // Build or update active board
        var board = activeBoard ?? Board(id: uuid, name: hwName)
        board.fwMajor = major
        board.fwMinor = minor
        board.hwName = hwName
        if !uuid.isEmpty { board.id = uuid }
        board.lastConnected = Date()
        activeBoard = board
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
