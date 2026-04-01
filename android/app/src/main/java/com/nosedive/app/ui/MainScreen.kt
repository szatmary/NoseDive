package com.nosedive.app.ui

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nosedive.app.engine.NoseDiveEngine
import com.nosedive.app.engine.Telemetry

/**
 * Main dashboard — shows telemetry when connected, connect button otherwise.
 * Thin Compose shell reading state from the C++ engine.
 */
@Composable
fun MainScreen(engine: NoseDiveEngine) {
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
            DashboardScreen(telemetry, engine)
        } else {
            ConnectScreen()
        }
    }
}

@Composable
fun ConnectScreen() {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
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
        Button(onClick = { /* TODO: BLE scan */ }) {
            Text("Scan for Boards")
        }
        Spacer(modifier = Modifier.height(12.dp))
        OutlinedButton(onClick = { /* TODO: TCP connect dialog */ }) {
            Text("Connect via TCP")
        }
    }
}

@Composable
fun DashboardScreen(telemetry: Telemetry, engine: NoseDiveEngine) {
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
        TelemetryRow("MOSFET", "%.1f°C".format(telemetry.tempMosfet))
        TelemetryRow("Motor", "%.1f°C".format(telemetry.tempMotor))
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

@Composable
fun SetupWizardScreen(engine: NoseDiveEngine) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Text(
            text = "Setup Wizard",
            fontSize = 28.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text("New board detected. Let's set it up.")
        Spacer(modifier = Modifier.height(32.dp))

        if (!engine.hasRefloat) {
            Text("Refloat is not installed.")
            Spacer(modifier = Modifier.height(12.dp))
            Button(onClick = { engine.installRefloat() }) {
                Text("Install Refloat")
            }
        } else {
            Text("Refloat is installed ✓")
        }

        Spacer(modifier = Modifier.height(32.dp))
        OutlinedButton(onClick = { engine.dismissWizard() }) {
            Text("Done")
        }
    }
}
