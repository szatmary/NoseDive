package com.nosedive.app.ble

import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
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
        private const val TAG = "BLEService"
        val VESC_SERVICE_UUID: UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        val VESC_TX_CHAR_UUID: UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        val VESC_RX_CHAR_UUID: UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
    }

    private val mainHandler = Handler(Looper.getMainLooper())

    private val _discoveredDevices = MutableStateFlow<List<DiscoveredDevice>>(emptyList())
    val discoveredDevices: StateFlow<List<DiscoveredDevice>> = _discoveredDevices.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning: StateFlow<Boolean> = _isScanning.asStateFlow()

    private val _isConnected = MutableStateFlow(false)
    val isConnected: StateFlow<Boolean> = _isConnected.asStateFlow()

    private var bluetoothGatt: BluetoothGatt? = null
    private var txCharacteristic: BluetoothGattCharacteristic? = null
    private var scanner: BluetoothLeScanner? = null

    // Write queue — GATT only allows one outstanding write at a time
    private val writeQueue = ArrayDeque<ByteArray>()
    private var isWritePending = false

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
        synchronized(writeQueue) { writeQueue.clear(); isWritePending = false }
        // close() may skip the GATT callback, so fire explicitly
        mainHandler.post {
            _isConnected.value = false
            onDisconnected?.invoke()
        }
    }

    fun send(data: ByteArray) {
        val mtu = ((bluetoothGatt?.mtu ?: 23) - 3).coerceAtLeast(20)
        var offset = 0
        synchronized(writeQueue) {
            while (offset < data.size) {
                val end = minOf(offset + mtu, data.size)
                writeQueue.addLast(data.copyOfRange(offset, end))
                offset = end
            }
        }
        drainWriteQueue()
    }

    private fun drainWriteQueue() {
        val gatt = bluetoothGatt ?: return
        val tx = txCharacteristic ?: return
        val chunk: ByteArray
        synchronized(writeQueue) {
            if (isWritePending || writeQueue.isEmpty()) return
            isWritePending = true
            chunk = writeQueue.removeFirst()
        }
        tx.value = chunk
        if (!gatt.writeCharacteristic(tx)) {
            Log.e(TAG, "writeCharacteristic failed")
            synchronized(writeQueue) { isWritePending = false }
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
            if (status != BluetoothGatt.GATT_SUCCESS && newState != BluetoothProfile.STATE_CONNECTED) {
                Log.e(TAG, "GATT error: status=$status newState=$newState")
                mainHandler.post {
                    _isConnected.value = false
                    onDisconnected?.invoke()
                }
                return
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                gatt.requestMtu(512)
                gatt.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                synchronized(writeQueue) { writeQueue.clear(); isWritePending = false }
                mainHandler.post {
                    _isConnected.value = false
                    onDisconnected?.invoke()
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Service discovery failed: $status")
                return
            }
            val service = gatt.getService(VESC_SERVICE_UUID) ?: return
            txCharacteristic = service.getCharacteristic(VESC_TX_CHAR_UUID)
            val rx = service.getCharacteristic(VESC_RX_CHAR_UUID) ?: return
            gatt.setCharacteristicNotification(rx, true)
            val descriptor = rx.getDescriptor(UUID.fromString("00002902-0000-1000-8000-00805f9b34fb"))
            if (descriptor != null) {
                descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                gatt.writeDescriptor(descriptor)
            }
            mainHandler.post {
                _isConnected.value = true
                onConnected?.invoke()
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.e(TAG, "Characteristic write failed: $status")
            }
            synchronized(writeQueue) { isWritePending = false }
            drainWriteQueue()
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (characteristic.uuid == VESC_RX_CHAR_UUID) {
                val data = characteristic.value
                mainHandler.post { onPayloadReceived?.invoke(data) }
            }
        }
    }
}
