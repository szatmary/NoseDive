package com.nosedive.app.ble

import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.ParcelUuid
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.UUID

data class DiscoveredDevice(
    val name: String,
    val address: String,
    val rssi: Int
)

class BLEService(private val context: Context) {
    companion object {
        val VESC_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val VESC_TX_CHAR_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val VESC_RX_CHAR_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    }

    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected.asStateFlow()

    private var bluetoothGatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null
    private var scanner: BluetoothLeScanner? = null

    var onPayloadReceived: ((ByteArray) -> Unit)? = null
    var onConnected: (() -> Unit)? = null
    var onDisconnected: (() -> Unit)? = null

    private val adapter: BluetoothAdapter?
        get() = (context.getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter

    fun startScan() {
        val s = adapter?.bluetoothLeScanner ?: return
        scanner = s
        _discoveredDevices.value = emptyList()
        _isScanning.value = true

        val filter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(VESC_SERVICE_UUID))
            .build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        s.startScan(listOf(filter), settings, scanCallback)
    }

    fun stopScan() {
        scanner?.stopScan(scanCallback)
        _isScanning.value = false
    }

    fun connect(address: String) {
        stopScan()
        val device = adapter?.getRemoteDevice(address) ?: return
        bluetoothGatt = device.connectGatt(context, false, gattCallback)
    }

    fun disconnect() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        txCharacteristic = null
        _isConnected.value = false
    }

    fun send(data: ByteArray) {
        val gatt = bluetoothGatt ?: return
        val tx = txCharacteristic ?: return
        val mtu = (gatt.mtu - 3).coerceAtLeast(20)
        var offset = 0
        while (offset < data.size) {
            val end = minOf(offset + mtu, data.size)
            tx.value = data.copyOfRange(offset, end)
            gatt.writeCharacteristic(tx)
            offset = end
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val name = device.name ?: return
            val existing = _discoveredDevices.value
            if (existing.none { it.address == device.address }) {
                _discoveredDevices.value = existing + DiscoveredDevice(name, device.address, result.rssi)
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                gatt.requestMtu(512)
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                _isConnected.value = false
                onDisconnected?.invoke()
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val service = gatt.getService(VESC_SERVICE_UUID) ?: return
            txCharacteristic = service.getCharacteristic(VESC_TX_CHAR_UUID)
            val rx = service.getCharacteristic(VESC_RX_CHAR_UUID) ?: return
            gatt.setCharacteristicNotification(rx, true)
            val descriptor = rx.getDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
            descriptor?.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
            _isConnected.value = true
            onConnected?.invoke()
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == VESC_RX_CHAR_UUID) {
                onPayloadReceived?.invoke(characteristic.value)
            }
        }
    }
}
