import Foundation
import CoreBluetooth

struct DiscoveredDevice: Identifiable {
    var id: String { identifier }
    let identifier: String
    let name: String
    let rssi: Int
}

class BLEService: NSObject {
    enum Event {
        case discovered(DiscoveredDevice)
        case connected
        case disconnected
        case telemetry(BoardTelemetry)
        case refloat(RefloatState)
    }

    // VESC BLE UUIDs
    static let vescServiceUUID = CBUUID(string: "6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    static let vescTxCharUUID  = CBUUID(string: "6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    static let vescRxCharUUID  = CBUUID(string: "6E400003-B5A3-F393-E0A9-E50E24DCCA9E")

    private var centralManager: CBCentralManager?
    private var connectedPeripheral: CBPeripheral?
    private var txCharacteristic: CBCharacteristic?
    private var rxCharacteristic: CBCharacteristic?

    private let eventHandler: (Event) -> Void
    private var packetBuffer = Data()

    init(eventHandler: @escaping (Event) -> Void) {
        self.eventHandler = eventHandler
        super.init()
        centralManager = CBCentralManager(delegate: self, queue: .global(qos: .userInitiated))
    }

    func startScan() {
        guard centralManager?.state == .poweredOn else { return }
        centralManager?.scanForPeripherals(
            withServices: [Self.vescServiceUUID],
            options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
        )
    }

    func stopScan() {
        centralManager?.stopScan()
    }

    func connect(to identifier: String) {
        guard let central = centralManager else { return }
        let peripherals = central.retrievePeripherals(withIdentifiers: [UUID(uuidString: identifier)].compactMap { $0 })
        guard let peripheral = peripherals.first else { return }
        connectedPeripheral = peripheral
        peripheral.delegate = self
        central.connect(peripheral, options: nil)
    }

    func disconnect() {
        if let p = connectedPeripheral {
            centralManager?.cancelPeripheralConnection(p)
        }
        connectedPeripheral = nil
        txCharacteristic = nil
        rxCharacteristic = nil
    }

    func send(_ data: Data) {
        guard let peripheral = connectedPeripheral,
              let tx = txCharacteristic else { return }
        // Fragment into MTU-sized chunks
        let mtu = peripheral.maximumWriteValueLength(for: .withoutResponse)
        var offset = 0
        while offset < data.count {
            let end = min(offset + mtu, data.count)
            let chunk = data[offset..<end]
            peripheral.writeValue(Data(chunk), for: tx, type: .withoutResponse)
            offset = end
        }
    }
}

// MARK: - CBCentralManagerDelegate

extension BLEService: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        // Ready to scan when powered on
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? "Unknown"
        let device = DiscoveredDevice(
            identifier: peripheral.identifier.uuidString,
            name: name,
            rssi: RSSI.intValue
        )
        eventHandler(.discovered(device))
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        peripheral.discoverServices([Self.vescServiceUUID])
        eventHandler(.connected)
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        eventHandler(.disconnected)
    }
}

// MARK: - CBPeripheralDelegate

extension BLEService: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.vescServiceUUID }) else { return }
        peripheral.discoverCharacteristics([Self.vescTxCharUUID, Self.vescRxCharUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for char in service.characteristics ?? [] {
            switch char.uuid {
            case Self.vescTxCharUUID:
                txCharacteristic = char
            case Self.vescRxCharUUID:
                rxCharacteristic = char
                peripheral.setNotifyValue(true, for: char)
            default:
                break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == Self.vescRxCharUUID,
              let data = characteristic.value else { return }
        // Accumulate and parse VESC packets
        packetBuffer.append(data)
        processPacketBuffer()
    }

    private func processPacketBuffer() {
        // Simple VESC packet extraction
        while let startIdx = packetBuffer.firstIndex(of: 0x02) ?? packetBuffer.firstIndex(of: 0x03) {
            // Remove garbage before start byte
            if startIdx > packetBuffer.startIndex {
                packetBuffer.removeSubrange(packetBuffer.startIndex..<startIdx)
            }
            guard packetBuffer.count >= 3 else { return }

            let isShort = packetBuffer[packetBuffer.startIndex] == 0x02
            let headerLen = isShort ? 2 : 3
            let payloadLen: Int
            if isShort {
                payloadLen = Int(packetBuffer[packetBuffer.startIndex + 1])
            } else {
                payloadLen = Int(packetBuffer[packetBuffer.startIndex + 1]) << 8 | Int(packetBuffer[packetBuffer.startIndex + 2])
            }

            let totalLen = headerLen + payloadLen + 3 // header + payload + crc(2) + end(1)
            guard packetBuffer.count >= totalLen else { return }

            // Extract and remove the packet
            let packetData = packetBuffer.prefix(totalLen)
            packetBuffer.removeSubrange(packetBuffer.startIndex..<packetBuffer.startIndex + totalLen)

            // Verify end byte
            guard packetData.last == 0x03 else { continue }

            // TODO: CRC check + dispatch payload to command parser
            // For now this is a skeleton — will integrate with C++ lib
        }
    }
}
