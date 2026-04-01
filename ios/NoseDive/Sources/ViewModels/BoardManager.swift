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
    @Published var refloatInstalling = false
    @Published var refloatInstalled = false

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
            // Queue CAN device IDs for sequential FW queries
            pendingCANQueries = canDevices.filter { $0 != 0 }
            // Start querying the first one
            queryNextCANDevice()

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

        case VESCPacket.CommPacketID.writeNewAppData.rawValue:
            // Refloat install complete — re-query FW version and Refloat info
            refloatInstalling = false
            refloatInstalled = true
            tcpTransport?.requestFWVersion()
            tcpTransport?.requestRefloatInfo()

        default:
            break
        }
    }

    /// The ID we're currently expecting a FW response from.
    /// nil means the next FW response is for the main VESC (direct query).
    private var awaitingFWResponseFrom: UInt8?

    private func queryNextCANDevice() {
        guard !pendingCANQueries.isEmpty else {
            awaitingFWResponseFrom = nil
            return
        }
        let nextID = pendingCANQueries.removeFirst()
        awaitingFWResponseFrom = nextID
        tcpTransport?.requestFWVersionForCAN(targetID: nextID)
    }

    private func handleFWVersion(_ info: VESCPacket.FWVersionInfo) {
        // Use the pending query tracker to determine which device this response belongs to.
        let controllerID: UInt8
        if let pending = awaitingFWResponseFrom {
            controllerID = pending
            awaitingFWResponseFrom = nil
            // Query the next CAN device in the queue
            queryNextCANDevice()
        } else {
            // No pending CAN query — this is the main VESC's direct response
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

    // MARK: - Refloat Install

    /// Whether the main VESC has Refloat installed (customConfigCount > 0).
    var hasRefloat: Bool {
        (deviceFWInfo[0]?.fwInfo?.customConfigCount ?? 0) > 0
    }

    /// Trigger Refloat install: erase → write (simulator handles this atomically).
    func installRefloat() {
        refloatInstalling = true
        refloatInstalled = false
        tcpTransport?.eraseNewApp()
        // Small delay then write — simulator processes erase synchronously
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { [weak self] in
            self?.tcpTransport?.writeNewAppData()
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
