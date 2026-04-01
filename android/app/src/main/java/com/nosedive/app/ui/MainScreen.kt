package com.nosedive.app.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nosedive.app.ble.BLEService
import com.nosedive.app.ble.DiscoveredDevice
import com.nosedive.app.engine.NoseDiveEngine
import com.nosedive.app.engine.Telemetry

/**
 * Main dashboard -- shows telemetry when connected, connect/scan screen otherwise.
 * Thin Compose shell reading state from the C++ engine and BLE service.
 */
@Composable
fun MainScreen(engine: NoseDiveEngine, bleService: BLEService) {
    val telemetry by engine.telemetry.collectAsState()
    val hasActiveBoard by engine.hasActiveBoard.collectAsState()
    val showWizard by engine.showWizard.collectAsState()

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background
    ) {
        if (showWizard) {
            SetupWizardScreen(engine)
        } else if (hasActiveBoard) {
            DashboardScreen(telemetry, engine, bleService)
        } else {
            ConnectScreen(engine, bleService)
        }
    }
}

@Composable
fun ConnectScreen(engine: NoseDiveEngine, bleService: BLEService) {
    val discoveredDevices by bleService.discoveredDevices.collectAsState()
    val isScanning by bleService.isScanning.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            text = "NoseDive",
            fontSize = 32.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text(
            text = "Connect to your board to get started",
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        Spacer(modifier = Modifier.height(32.dp))

        if (isScanning) {
            Button(onClick = { bleService.stopScan() }) {
                Text("Stop Scanning")
            }
            Spacer(modifier = Modifier.height(8.dp))
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        } else {
            Button(onClick = { bleService.startScan() }) {
                Text("Scan for Boards")
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        if (discoveredDevices.isNotEmpty()) {
            Text(
                text = "Discovered Devices",
                fontWeight = FontWeight.Medium,
                modifier = Modifier.align(Alignment.Start)
            )
            Spacer(modifier = Modifier.height(8.dp))
            LazyColumn {
                items(discoveredDevices) { device ->
                    DeviceRow(device) {
                        bleService.connect(device.address)
                    }
                }
            }
        } else if (isScanning) {
            Text(
                text = "Scanning...",
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Spacer(modifier = Modifier.height(12.dp))
        OutlinedButton(onClick = { /* TODO: TCP connect dialog */ }) {
            Text("Connect via TCP")
        }
    }
}

@Composable
fun DeviceRow(device: DiscoveredDevice, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
            .clickable { onClick() }
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(text = device.name, fontWeight = FontWeight.Medium)
                Text(
                    text = device.address,
                    fontSize = 12.sp,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Text(
                text = "${device.rssi} dBm",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
fun DashboardScreen(telemetry: Telemetry, engine: NoseDiveEngine, bleService: BLEService) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Speed
        Text(
            text = "%.1f".format(engine.speedKmh),
            fontSize = 64.sp,
            fontWeight = FontWeight.Bold
        )
        Text(
            text = "km/h",
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )

        Spacer(modifier = Modifier.height(32.dp))

        // Battery
        TelemetryRow("Battery", "%.0f%%".format(telemetry.batteryPercent))
        TelemetryRow("Voltage", "%.1fV".format(telemetry.batteryVoltage))
        TelemetryRow("Power", "%.0fW".format(telemetry.power))
        TelemetryRow("Duty", "%.1f%%".format(telemetry.dutyCycle * 100))
        TelemetryRow("MOSFET", "%.1f\u00B0C".format(telemetry.tempMosfet))
        TelemetryRow("Motor", "%.1f\u00B0C".format(telemetry.tempMotor))

        // Additional telemetry fields
        TelemetryRow("Motor Current", "%.1fA".format(telemetry.motorCurrent))
        TelemetryRow("Battery Current", "%.1fA".format(telemetry.batteryCurrent))
        TelemetryRow("Tachometer", "%d".format(telemetry.tachometer))

        // Fault display
        if (telemetry.fault != 0) {
            Spacer(modifier = Modifier.height(16.dp))
            Card(
                colors = CardDefaults.cardColors(
                    containerColor = MaterialTheme.colorScheme.errorContainer
                ),
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = "Fault Code: ${telemetry.fault}",
                    modifier = Modifier.padding(16.dp),
                    color = MaterialTheme.colorScheme.onErrorContainer,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        Spacer(modifier = Modifier.weight(1f))

        // Disconnect button
        OutlinedButton(
            onClick = { bleService.disconnect() },
            colors = ButtonDefaults.outlinedButtonColors(
                contentColor = MaterialTheme.colorScheme.error
            ),
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Disconnect")
        }
    }
}

@Composable
fun TelemetryRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(text = label, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(text = value, fontWeight = FontWeight.Medium)
    }
}

// SetupWizardScreen is defined in SetupWizardScreen.kt
