import SwiftUI
import Combine
import Foundation
import CNoseDive

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
    @Published var refloatInstalling = false
    @Published var refloatInstalled = false

    // Profiles
    @Published var riderProfiles: [RiderProfile] = RiderProfile.builtInPresets
    @Published var activeProfile: RiderProfile = .flow

    // Transport
    private var bleService: BLEService?
    private var tcpTransport: TCPTransport?
    private var vescTransport: OpaquePointer? // nd_transport_t* for VESC packet framing (BLE and TCP)
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

        // Set up engine callbacks
        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        nd_engine_set_send_callback(engine, { payload, len, ctx in
            guard let ctx, let payload else { return }
            let data = Data(bytes: payload, count: len)
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                mgr.sendToTransport(data)
            }
        }, selfPtr)

        nd_engine_set_state_callback(engine, { ctx in
            guard let ctx else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                mgr.refreshFromEngine()
            }
        }, selfPtr)

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

    // MARK: - Engine state refresh

    private func refreshFromEngine() {
        // Telemetry
        let ct = nd_engine_get_telemetry(engine)
        telemetry = NoseDiveBridge.telemetryFromC(ct)

        // Active board
        if nd_engine_has_active_board(engine) {
            let cb = nd_engine_get_active_board(engine)
            activeBoard = NoseDiveBridge.boardFromC(cb)
        } else {
            activeBoard = nil
        }

        // CAN devices
        let canCount = nd_engine_can_device_count(engine)
        canDevices = (0..<canCount).map { nd_engine_can_device_id(engine, $0) }

        // Main firmware info
        if nd_engine_has_active_board(engine) {
            let fw = nd_engine_get_main_fw(engine)
            if fw.major > 0 || fw.minor > 0 {
                mainFWInfo = FWVersionInfo(
                    hwName: String(cString: fw.hw_name),
                    major: fw.major,
                    minor: fw.minor,
                    uuid: String(cString: fw.uuid),
                    hwType: fw.hw_type,
                    customConfigCount: fw.custom_config_count,
                    packageName: String(cString: fw.package_name)
                )
            } else {
                mainFWInfo = nil
            }
        } else {
            mainFWInfo = nil
        }

        // Wizard
        showWizard = nd_engine_should_show_wizard(engine)

        // Refloat
        if nd_engine_has_refloat(engine) {
            let ri = nd_engine_get_refloat_info(engine)
            refloatInfo = RefloatInfo(
                name: String(cString: ri.name),
                major: ri.major,
                minor: ri.minor,
                patch: ri.patch,
                suffix: String(cString: ri.suffix)
            )
        } else {
            refloatInfo = nil
        }
        refloatInstalling = nd_engine_refloat_installing(engine)
        refloatInstalled = nd_engine_refloat_installed(engine)

        // Fleet
        loadBoardsFromEngine()
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

    // MARK: - Transport send

    private func sendToTransport(_ data: Data) {
        guard let transport = vescTransport else { return }
        data.withUnsafeBytes { buf in
            guard let ptr = buf.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            nd_transport_send_payload(transport, ptr, buf.count)
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
        setupVESCTransport(mtu: 20)
        bleService?.connect(to: device.identifier)
    }

    // MARK: - TCP Connection

    func connectTCP(host: String, port: UInt16) {
        disconnect()
        connectionState = .connecting("\(host):\(port)")
        activeTransport = .tcp
        // TCP has no MTU limit — use large value so framing sends whole packets
        setupVESCTransport(mtu: 4096)

        let transport = TCPTransport { [weak self] event in
            Task { @MainActor in
                self?.handleTCPEvent(event)
            }
        }
        tcpTransport = transport
        transport.connect(host: host, port: port)
    }

    // MARK: - VESC Transport (shared by BLE and TCP)

    private func setupVESCTransport(mtu: Int) {
        teardownVESCTransport()
        let transport = nd_transport_create(mtu)
        vescTransport = transport

        let selfPtr = Unmanaged.passUnretained(self).toOpaque()

        // Transport send callback → write framed chunks to the active link
        nd_transport_set_send_callback(transport, { data, len, ctx in
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

        // Transport packet callback → complete VESC payload → engine
        nd_transport_set_packet_callback(transport, { payload, len, ctx in
            guard let ctx, let payload else { return }
            let mgr = Unmanaged<BoardManager>.fromOpaque(ctx).takeUnretainedValue()
            Task { @MainActor in
                nd_engine_handle_payload(mgr.engine, payload, len)
            }
        }, selfPtr)
    }

    private func teardownVESCTransport() {
        if let transport = vescTransport {
            nd_transport_destroy(transport)
            vescTransport = nil
        }
    }

    // MARK: - Disconnect

    func disconnect() {
        teardownVESCTransport()
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
        refreshFromEngine()
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
        nd_engine_has_refloat(engine)
    }

    func installRefloat() {
        nd_engine_install_refloat(engine)
    }

    // MARK: - Board identification

    var guessedBoardType: String? {
        guard let cstr = nd_engine_guessed_board_type(engine) else { return nil }
        return String(cString: cstr)
    }

    var isKnownBoard: Bool {
        nd_engine_is_known_board(engine)
    }

    // MARK: - Wizard

    func dismissWizard() {
        nd_engine_dismiss_wizard(engine)
        showWizard = false
    }

    // MARK: - Computed

    var isConnected: Bool {
        connectionState == .connected
    }

    var speedKmh: Double {
        nd_engine_speed_kmh(engine)
    }

    var speedMph: Double {
        nd_engine_speed_mph(engine)
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
            nd_engine_on_connected(engine)
        case .disconnected:
            teardownVESCTransport()
            connectionState = .disconnected
            activeTransport = .none
            nd_engine_on_disconnected(engine)
            refreshFromEngine()
        case .data(let rawData):
            feedTransport(rawData)
        }
    }

    // MARK: - TCP Events

    private func handleTCPEvent(_ event: TCPTransport.Event) {
        switch event {
        case .connected:
            connectionState = .connected
            nd_engine_on_connected(engine)

        case .disconnected:
            teardownVESCTransport()
            connectionState = .disconnected
            activeTransport = .none
            tcpTransport = nil
            nd_engine_on_disconnected(engine)
            refreshFromEngine()

        case .data(let rawData):
            feedTransport(rawData)
        }
    }

    // MARK: - Transport data feed (shared by BLE and TCP)

    private func feedTransport(_ rawData: Data) {
        guard let transport = vescTransport else { return }
        rawData.withUnsafeBytes { buf in
            guard let ptr = buf.baseAddress?.assumingMemoryBound(to: UInt8.self) else { return }
            nd_transport_receive(transport, ptr, buf.count)
        }
    }
}
