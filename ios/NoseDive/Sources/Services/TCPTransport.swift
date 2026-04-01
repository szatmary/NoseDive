import Foundation
import Network

/// TCP transport for connecting to a VESC simulator or TCP-bridged VESC.
/// Uses Network.framework (NWConnection) for async TCP I/O.
class TCPTransport {
    enum Event {
        case connected
        case disconnected(Error?)
        case packet(Data) // complete VESC payload (decoded, CRC-verified)
    }

    private var connection: NWConnection?
    private var receiveBuffer = Data()
    private let eventHandler: (Event) -> Void
    private let queue = DispatchQueue(label: "com.nosedive.tcp", qos: .userInitiated)
    private var polling: Bool = false

    init(eventHandler: @escaping (Event) -> Void) {
        self.eventHandler = eventHandler
    }

    /// Connect to a VESC/simulator at host:port.
    func connect(host: String, port: UInt16) {
        let nwHost = NWEndpoint.Host(host)
        let nwPort = NWEndpoint.Port(rawValue: port)!
        let conn = NWConnection(host: nwHost, port: nwPort, using: .tcp)
        connection = conn

        conn.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                self?.eventHandler(.connected)
                self?.startReceive()
            case .failed(let error):
                self?.eventHandler(.disconnected(error))
            case .cancelled:
                self?.eventHandler(.disconnected(nil))
            default:
                break
            }
        }

        conn.start(queue: queue)
    }

    func disconnect() {
        connection?.cancel()
        connection = nil
        receiveBuffer = Data()
    }

    /// Send a raw VESC payload (will be framed with CRC).
    func send(_ payload: Data) {
        guard let conn = connection else { return }
        let packet = VESCPacket.encode(payload)
        conn.send(content: packet, completion: .contentProcessed { error in
            if let error {
                print("TCPTransport send error: \(error)")
            }
        })
    }

    /// Send a COMM_GET_VALUES request.
    func requestValues() {
        send(Data([VESCPacket.CommPacketID.getValues.rawValue]))
    }

    /// Send a COMM_PING_CAN request.
    func requestPingCAN() {
        send(Data([VESCPacket.CommPacketID.pingCAN.rawValue]))
    }

    /// Send a COMM_FW_VERSION request for a CAN device.
    func requestFWVersionForCAN(targetID: UInt8) {
        send(VESCPacket.buildFWVersionRequestForCAN(targetID: targetID))
    }

    /// Send a Refloat info request.
    func requestRefloatInfo() {
        send(VESCPacket.buildRefloatInfoRequest())
    }

    /// Send a COMM_FW_VERSION request.
    func requestFWVersion() {
        send(Data([VESCPacket.CommPacketID.fwVersion.rawValue]))
    }

    /// Send COMM_ERASE_NEW_APP to erase the custom app slot.
    func eraseNewApp() {
        send(Data([VESCPacket.CommPacketID.eraseNewApp.rawValue]))
    }

    /// Send COMM_WRITE_NEW_APP_DATA to install the custom app.
    func writeNewAppData() {
        send(Data([VESCPacket.CommPacketID.writeNewAppData.rawValue, 0x00]))
    }

    // MARK: - Polling

    /// Start periodic telemetry polling at the given interval.
    func startPolling(interval: TimeInterval = 0.1) {
        polling = true
        poll(interval: interval)
    }

    func stopPolling() {
        polling = false
    }

    private func poll(interval: TimeInterval) {
        guard polling else { return }
        requestValues()
        queue.asyncAfter(deadline: .now() + interval) { [weak self] in
            self?.poll(interval: interval)
        }
    }

    // MARK: - Receive

    private func startReceive() {
        guard let conn = connection else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] content, _, isComplete, error in
            if let data = content {
                self?.receiveBuffer.append(data)
                self?.processBuffer()
            }
            if isComplete || error != nil {
                self?.eventHandler(.disconnected(error))
                return
            }
            self?.startReceive()
        }
    }

    private func processBuffer() {
        // Skip to first valid start byte
        while !receiveBuffer.isEmpty {
            let first = receiveBuffer[receiveBuffer.startIndex]
            if first == 0x02 || first == 0x03 {
                break
            }
            receiveBuffer.removeFirst()
        }

        // Extract as many complete packets as possible
        while true {
            guard let result = VESCPacket.decode(receiveBuffer) else { break }
            receiveBuffer.removeFirst(result.consumed)
            eventHandler(.packet(result.payload))
        }
    }
}
