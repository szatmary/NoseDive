import Foundation
import Network

/// Raw TCP pipe for connecting to a VESC simulator or TCP-bridged VESC.
/// Uses Network.framework (NWConnection) for async TCP I/O.
///
/// This is a thin transport — it does not handle VESC packet framing.
/// Framing is done by the C++ nd_transport_t layer, same as BLE.
class TCPTransport {
    enum Event {
        case connected
        case disconnected(Error?)
        case data(Data) // raw bytes from TCP stream
    }

    private var connection: NWConnection?
    private let eventHandler: (Event) -> Void
    private let queue = DispatchQueue(label: "com.nosedive.tcp", qos: .userInitiated)

    init(eventHandler: @escaping (Event) -> Void) {
        self.eventHandler = eventHandler
    }

    /// Connect to a VESC/simulator at host:port.
    func connect(host: String, port: UInt16) {
        let nwHost = NWEndpoint.Host(host)
        guard let nwPort = NWEndpoint.Port(rawValue: port) else { return }
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
    }

    /// Send raw bytes over the TCP connection.
    func send(_ data: Data) {
        guard let conn = connection else { return }
        conn.send(content: data, completion: .contentProcessed { error in
            if let error {
                print("TCPTransport send error: \(error)")
            }
        })
    }

    // MARK: - Receive

    private func startReceive() {
        guard let conn = connection else { return }
        conn.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self] content, _, isComplete, error in
            if let data = content {
                self?.eventHandler(.data(data))
            }
            if isComplete || error != nil {
                self?.eventHandler(.disconnected(error))
                return
            }
            self?.startReceive()
        }
    }
}
