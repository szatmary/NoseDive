import SwiftUI
import Combine

/// Firmware info for a single device on the CAN bus (or the main VESC).
struct DeviceFWInfo: Identifiable {
    var id: UInt8 { controllerID }
    let controllerID: UInt8
    var fwInfo: VESCPacket.FWVersionInfo?
    var refloatInfo: VESCPacket.RefloatInfo? // only for main VESC

    var displayName: String {
        if let hw = fwInfo?.hwName, !hw.isEmpty { return hw }
        switch controllerID {
        case 253: return "VESC Express"
        case 10: return "BMS"
        default: return "VESC \(controllerID)"
        }
    }

    var fwVersionString: String {
        guard let fw = fwInfo else { return "Unknown" }
        return "\(fw.major).\(fw.minor)"
    }

    var isExpress: Bool { fwInfo?.hwType == .vescExpress }
    var isBMS: Bool { controllerID == 10 }
    var hasRefloat: Bool { (fwInfo?.customConfigCount ?? 0) > 0 }
}

@MainActor
class BoardManager: ObservableObject {
    // Connection
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isScanning = false
    @Published var discoveredDevices: [DiscoveredDevice] = []
    @Published var canDevices: [UInt8] = []

    // Firmware info for all devices
    @Published var deviceFWInfo: [UInt8: DeviceFWInfo] = [:]
    @Published var refloatInfo: VESCPacket.RefloatInfo?

    // Active board
    @Published var activeBoard: Board?
    @Published var telemetry = BoardTelemetry()
    @Published var refloatState = RefloatState()

    // Fleet
    @Published var boards: [Board] = []

    // Profiles
    @Published var riderProfiles: [RiderProfile] = RiderProfile.builtInPresets
    @Published var activeProfile: RiderProfile = .flow

    // Wizard
    @Published var showWizard = false

    // Transport
    private var bleService: BLEService?
    private var tcpTransport: TCPTransport?
    private var activeTransport: TransportKind = .none
    private var pendingCANQueries: [UInt8] = []

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
        deviceFWInfo = [:]
        refloatInfo = nil
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

    // MARK: - Board Identification

    /// Try to guess what board this is from FW version info.
    var guessedBoardType: String? {
        guard let mainFW = deviceFWInfo[0]?.fwInfo else { return nil }
        let hw = mainFW.hwName.lowercased()

        if hw.contains("thor") { return "Funwheel X7" }
        if hw.contains("ubox") { return "DIY Ubox Build" }
        if hw.contains("little focer") { return "XR VESC Conversion" }
        if hw.contains("75/300") || hw.contains("100/250") { return "Trampa VESC Build" }
        if hw.contains("mk6") || hw.contains("mk5") || hw.contains("mk4") { return "VESC Build" }

        return nil
    }

    /// Check if this board has been seen before.
    var isKnownBoard: Bool {
        guard let mainFW = deviceFWInfo[0]?.fwInfo else { return false }
        return boards.contains { $0.id == mainFW.uuid }
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
            // Discovery sequence: FW version → CAN scan → Refloat info
            tcpTransport?.requestFWVersion()
            tcpTransport?.requestPingCAN()
            tcpTransport?.requestRefloatInfo()
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
                if let board = activeBoard {
                    t.speed = board.speedFromERPM(t.erpm)
                    t.batteryPercent = board.batteryPercent(voltage: t.batteryVoltage)
                }
                telemetry = t
            }

        case VESCPacket.CommPacketID.fwVersion.rawValue:
            if let info = VESCPacket.parseFWVersion(payload) {
                handleFWVersion(info)
            }

        case VESCPacket.CommPacketID.pingCAN.rawValue:
            canDevices = VESCPacket.parsePingCAN(payload)
            // Query FW version for each CAN device
            for id in canDevices where id != 0 {
                tcpTransport?.requestFWVersionForCAN(targetID: id)
            }

        case VESCPacket.CommPacketID.customAppData.rawValue:
            if let info = VESCPacket.parseRefloatInfo(payload) {
                refloatInfo = info
                if var dev = deviceFWInfo[0] {
                    dev.refloatInfo = info
                    deviceFWInfo[0] = dev
                }
                // Update active board with Refloat version
                if var board = activeBoard {
                    board.refloatVersion = info.versionString
                    activeBoard = board
                }
            }

        default:
            break
        }
    }

    private func handleFWVersion(_ info: VESCPacket.FWVersionInfo) {
        // Determine which device this came from based on HW type and existing knowledge
        // The main VESC responds directly; CAN devices respond via forward
        let controllerID: UInt8
        if info.hwType == .vescExpress {
            controllerID = 253
        } else if deviceFWInfo[0] == nil || deviceFWInfo[0]?.fwInfo == nil {
            // First FW version response is the main VESC
            controllerID = 0
        } else if info.uuid != deviceFWInfo[0]?.fwInfo?.uuid {
            // Different UUID = different device. Check CAN IDs.
            if let bmsID = canDevices.first(where: { $0 == 10 }), deviceFWInfo[10] == nil {
                controllerID = bmsID
            } else {
                controllerID = 0
            }
        } else {
            controllerID = 0
        }

        var dev = deviceFWInfo[controllerID] ?? DeviceFWInfo(controllerID: controllerID)
        dev.fwInfo = info
        deviceFWInfo[controllerID] = dev

        // Update active board from main VESC
        if controllerID == 0 {
            var board = activeBoard ?? Board(id: info.uuid, name: info.hwName.isEmpty ? "VESC Board" : info.hwName)
            board.fwMajor = info.major
            board.fwMinor = info.minor
            board.hwName = info.hwName
            if !info.uuid.isEmpty { board.id = info.uuid }
            board.lastConnected = Date()
            activeBoard = board

            // If board is unknown, prompt for wizard
            if !isKnownBoard {
                showWizard = true
            }
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
