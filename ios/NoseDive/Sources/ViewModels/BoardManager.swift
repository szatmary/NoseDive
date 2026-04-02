import SwiftUI
import Combine
import Foundation
import CNoseDive

/// Convert a C fixed-size char array (tuple) to a Swift String.
/// Works with any tuple of CChar/Int8 up to the tuple's size.
func cString<T>(_ tuple: T) -> String {
    withUnsafePointer(to: tuple) { ptr in
        ptr.withMemoryRebound(to: CChar.self, capacity: MemoryLayout<T>.size) {
            String(cString: $0)
        }
    }
}

/// Thin GUI shell around the C++ NoseDive engine.
/// All business logic lives in C++. This class handles:
/// - Platform transport (BLE/TCP) → feeds payloads to engine
/// - Reading engine state → publishes to SwiftUI
/// - Connection lifecycle
@MainActor
class BoardManager: ObservableObject {
    // Connection (platform-level)
    @Published var connectionState: ConnectionState = .disconnected
    @Published var isScanning = false
    @Published var discoveredDevices: [DiscoveredDevice] = []

    // State from engine (refreshed on engine callbacks)
    @Published var telemetry = BoardTelemetry()
    @Published var activeBoard: Board?
    @Published var boards: [Board] = []
    @Published var canDevices: [UInt8] = []
    @Published var showWizard = false
    @Published var refloatInfo: RefloatInfo?
    @Published var mainFWInfo: FWVersionInfo?
    @Published var refloatState = RefloatState()
    @Published var refloatInstalling = false
    @Published var refloatInstalled = false

    // Profiles
    @Published var riderProfiles: [RiderProfile] = RiderProfile.builtInPresets
    @Published var activeProfile: RiderProfile = .flow

    // Transport
    private var bleService: BLEService?
    private var tcpTransport: TCPTransport?
    // (transport removed — engine owns packet codec internally)
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

    // C++ engine handle
    private let engine: OpaquePointer

    init() {
        engine = NoseDiveEngine.shared.handle

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        // Write callback — engine sends raw framed bytes, platform writes to wire
        nd_engine_set_write_callback(engine, { data, len, ctx in
            guard let ctx, let data else { return }
            let chunk = Data(bytes: data, count: len)
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                switch mgr.activeTransport {
                case .ble:  mgr.bleService?.send(chunk)
                case .tcp:  mgr.tcpTransport?.send(chunk)
                case .none: break
                }
            }
        }, selfPtr)

        // Telemetry callback
        nd_engine_set_telemetry_callback(engine, { t, ctx in
            guard let ctx else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                mgr.telemetry = BoardTelemetry(
                    speed: t.speed,
                    dutyCycle: t.duty_cycle,
                    batteryVoltage: t.battery_voltage,
                    batteryPercent: t.battery_percent,
                    motorCurrent: t.motor_current,
                    batteryCurrent: t.battery_current,
                    power: t.power,
                    mosfetTemp: t.temp_mosfet,
                    motorTemp: t.temp_motor,
                    erpm: t.erpm,
                    tachometer: t.tachometer,
                    tachometerAbs: t.tachometer_abs,
                    fault: t.fault
                )
            }
        }, selfPtr)

        // Board callback — fires when FW_VERSION received
        nd_engine_set_board_callback(engine, { board, ctx in
            guard let ctx else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                mgr.mainFWInfo = FWVersionInfo(
                    hwName: cString(board.hw_name),
                    major: board.fw_major,
                    minor: board.fw_minor,
                    uuid: cString(board.uuid),
                    hwType: board.hw_type,
                    customConfigCount: board.custom_config_count,
                    packageName: cString(board.package_name)
                )
                mgr.showWizard = board.show_wizard
                mgr.loadBoardsFromEngine()
            }
        }, selfPtr)

        // Refloat callback
        nd_engine_set_refloat_callback(engine, { info, ctx in
            guard let ctx else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                if info.has_refloat {
                    mgr.refloatInfo = RefloatInfo(
                        name: cString(info.name),
                        major: info.major,
                        minor: info.minor,
                        patch: info.patch,
                        suffix: cString(info.suffix)
                    )
                } else {
                    mgr.refloatInfo = nil
                }
                mgr.refloatInstalling = info.installing
                mgr.refloatInstalled = info.installed
            }
        }, selfPtr)

        // CAN callback
        nd_engine_set_can_callback(engine, { ids, count, ctx in
            guard let ctx else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            let deviceIds: [UInt8] = ids != nil ? Array(UnsafeBufferPointer(start: ids, count: count)) : []
            Task { @MainActor in
                mgr.canDevices = deviceIds
            }
        }, selfPtr)

        // Error callback
        nd_engine_set_error_callback(engine, { message, ctx in
            guard let message else { return }
            print("NoseDive engine: \(String(cString: message))")
        }, selfPtr)

        bleService = BLEService { [weak self] event in
            Task { @MainActor in
                self?.handleBLEEvent(event)
            }
        }

        loadProfilesFromEngine()
    }

    private func loadBoardsFromEngine() {
        let count = nd_engine_board_count(engine)
        boards = (0..<count).map { i in
            NoseDiveBridge.boardFromC(nd_engine_get_board(engine, i))
        }
    }

    private func loadProfilesFromEngine() {
        let count = nd_engine_profile_count(engine)
        let loaded: [RiderProfile] = (0..<count).map { i in
            NoseDiveBridge.profileFromC(nd_engine_get_profile(engine, i))
        }
        let userProfiles = loaded.filter { !$0.isBuiltIn }
        riderProfiles = RiderProfile.builtInPresets + userProfiles

        if let savedId = nd_engine_active_profile_id(engine) {
            let idStr = String(cString: savedId)
            if let match = riderProfiles.first(where: { $0.id.uuidString == idStr }) {
                activeProfile = match
            }
        }
    }

    // MARK: - Raw byte feed (platform → engine)

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
            tcpTransport?.disconnect()
            tcpTransport = nil
        case .none:
            break
        }
        activeTransport = .none
        connectionState = .disconnected
        nd_engine_on_disconnected(engine)
    }

    // MARK: - Profiles

    func selectProfile(_ profile: RiderProfile) {
        activeProfile = profile
        nd_engine_set_active_profile_id(engine, profile.id.uuidString)
    }

    func addProfile(_ profile: RiderProfile) {
        riderProfiles.append(profile)
        nd_engine_save_profile(engine, NoseDiveBridge.profileToC(profile))
    }

    func updateProfile(_ profile: RiderProfile) {
        if let idx = riderProfiles.firstIndex(where: { $0.id == profile.id }) {
            riderProfiles[idx] = profile
        }
        if activeProfile.id == profile.id {
            activeProfile = profile
        }
        nd_engine_save_profile(engine, NoseDiveBridge.profileToC(profile))
    }

    // MARK: - Board management

    func saveBoard(_ board: Board) {
        nd_engine_save_board(engine, NoseDiveBridge.boardToC(board))
        loadBoardsFromEngine()
    }

    // MARK: - Refloat install

    var hasRefloat: Bool {
        refloatInfo != nil
    }

    func installRefloat() {
        nd_engine_install_refloat(engine)
    }

    // MARK: - Wizard

    func dismissWizard() {
        nd_engine_dismiss_wizard(engine)
        showWizard = false
    }

    func saveToDisk() {
        // Engine auto-persists via storage layer
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

    // MARK: - BLE Events

    private func handleBLEEvent(_ event: BLEService.Event) {
        switch event {
        case .discovered(let device):
            if !discoveredDevices.contains(where: { $0.identifier == device.identifier }) {
                discoveredDevices.append(device)
            }
        case .connected:
            connectionState = .connected
            nd_engine_on_connected(engine, 512) // BLE MTU
        case .disconnected:
            connectionState = .disconnected
            activeTransport = .none
            nd_engine_on_disconnected(engine)
        case .data(let rawData):
            feedEngine(rawData)
        }
    }

    // MARK: - TCP Events

    private func handleTCPEvent(_ event: TCPTransport.Event) {
        switch event {
        case .connected:
            connectionState = .connected
            nd_engine_on_connected(engine, 4096) // TCP, no MTU limit

        case .disconnected:
            connectionState = .disconnected
            activeTransport = .none
            tcpTransport = nil
            nd_engine_on_disconnected(engine)

        case .data(let rawData):
            feedEngine(rawData)
        }
    }

    // MARK: - Raw byte feed (platform → engine)

    private func feedEngine(_ rawData: Data) {
        rawData.withUnsafeBytes { buf in
            guard let ptr = buf.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            nd_engine_receive_bytes(engine, ptr, buf.count)
        }
    }
}
