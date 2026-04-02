package com.nosedive.app.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nosedive.app.engine.FWVersionInfo
import com.nosedive.app.engine.NoseDiveEngine
import com.nosedive.app.engine.RefloatInfo

/**
 * Multi-step setup wizard matching iOS SetupWizardView.
 * Steps: Firmware Check → Identify → Refloat Check → Complete
 */

enum class WizardStep(val title: String) {
    FIRMWARE("Firmware"),
    IDENTIFY("Identify"),
    REFLOAT("Refloat"),
    COMPLETE("Complete");
}

private val wizardSteps = listOf(
    WizardStep.FIRMWARE,
    WizardStep.IDENTIFY,
    WizardStep.REFLOAT,
    WizardStep.COMPLETE,
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SetupWizardScreen(engine: NoseDiveEngine) {
    var currentIndex by remember { mutableIntStateOf(0) }
    val currentStep = wizardSteps[currentIndex]

    fun advance() {
        if (currentIndex < wizardSteps.size - 1) currentIndex++
    }

    fun goBack() {
        if (currentIndex > 0) currentIndex--
    }

    fun dismiss() {
        engine.saveBoard()
        engine.dismissWizard()
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text("Board Setup") },
                navigationIcon = {
                    if (currentIndex > 0 && currentStep != WizardStep.COMPLETE) {
                        IconButton(onClick = { goBack() }) {
                            Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back")
                        }
                    } else {
                        TextButton(onClick = { engine.dismissWizard() }) {
                            Text("Cancel")
                        }
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            // Step indicator
            if (wizardSteps.size > 2) {
                StepIndicator(
                    steps = wizardSteps,
                    currentIndex = currentIndex,
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp)
                )
            }

            // Step content
            AnimatedContent(
                targetState = currentStep,
                label = "wizard_step"
            ) { step ->
                when (step) {
                    WizardStep.FIRMWARE -> FirmwareCheckScreen(engine, onContinue = { advance() })
                    WizardStep.IDENTIFY -> IdentifyScreen(
                        engine,
                        onContinue = { advance() },
                        onSkip = { dismiss() }
                    )
                    WizardStep.REFLOAT -> RefloatCheckScreen(engine, onContinue = { advance() })
                    WizardStep.COMPLETE -> CompleteScreen(engine, onDone = { dismiss() })
                }
            }
        }
    }
}

@Composable
private fun StepIndicator(steps: List<WizardStep>, currentIndex: Int, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        steps.forEachIndexed { index, step ->
            Column(
                modifier = Modifier.weight(1f),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                LinearProgressIndicator(
                    progress = { if (index <= currentIndex) 1f else 0f },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(3.dp)
                        .clip(RoundedCornerShape(2.dp)),
                    color = MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.surfaceVariant,
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = step.title,
                    fontSize = 9.sp,
                    fontWeight = if (index == currentIndex) FontWeight.Bold else FontWeight.Normal,
                    color = if (index <= currentIndex)
                        MaterialTheme.colorScheme.primary
                    else
                        MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

// MARK: - Firmware Check Screen

@Composable
private fun FirmwareCheckScreen(engine: NoseDiveEngine, onContinue: () -> Unit) {
    val mainFW by engine.mainFW.collectAsState()
    val refloatInfo by engine.refloatInfo.collectAsState()
    val canDevices by engine.canDevices.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            Icons.Default.Refresh,
            contentDescription = null,
            modifier = Modifier.size(40.dp),
            tint = MaterialTheme.colorScheme.primary
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text("Firmware Check", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Spacer(modifier = Modifier.height(4.dp))
        Text(
            "Checking firmware versions for all devices.",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 12.sp
        )
        Spacer(modifier = Modifier.height(16.dp))

        // VESC Express (CAN ID 253)
        if (canDevices.contains(253)) {
            FirmwareCard(
                title = "VESC Express",
                icon = Icons.Default.Wifi,
                detail = "ESP32",
                currentFW = null, // Express FW not available via main FW query
                latestFW = "6.5"
            )
            Spacer(modifier = Modifier.height(8.dp))
        }

        // Main VESC
        val fw = mainFW
        if (fw != null) {
            FirmwareCard(
                title = "VESC Motor Controller",
                icon = Icons.Default.Memory,
                detail = fw.hwName,
                currentFW = fw.versionString,
                latestFW = "6.5"
            )
            Spacer(modifier = Modifier.height(8.dp))
        }

        // Refloat
        val ri = refloatInfo
        if (ri != null) {
            FirmwareCard(
                title = "Refloat Package",
                icon = Icons.Default.Surfing,
                detail = ri.name,
                currentFW = ri.versionString,
                latestFW = "2.0.1"
            )
        } else if (fw != null && fw.customConfigCount > 0) {
            FirmwareCard(
                title = "Refloat Package",
                icon = Icons.Default.Surfing,
                detail = null,
                currentFW = "Querying…",
                latestFW = "2.0.1"
            )
        } else {
            MissingRefloatCard(onInstall = { engine.installRefloat() })
        }
        Spacer(modifier = Modifier.height(8.dp))

        // BMS (CAN ID 10)
        if (canDevices.contains(10)) {
            FirmwareCard(
                title = "BMS",
                icon = Icons.Default.BatteryChargingFull,
                detail = "VESC BMS",
                currentFW = null,
                latestFW = "6.5"
            )
            Spacer(modifier = Modifier.height(8.dp))
        }

        Spacer(modifier = Modifier.height(16.dp))
        PrimaryButton("Continue", onClick = onContinue)
    }
}

@Composable
private fun FirmwareCard(
    title: String,
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    detail: String?,
    currentFW: String?,
    latestFW: String
) {
    val isUpToDate = currentFW != null && currentFW != "Querying…" && run {
        val cur = currentFW.split(".").map { it.toIntOrNull() ?: 0 }
        val lat = latestFW.split(".").map { it.toIntOrNull() ?: 0 }
        val maxLen = maxOf(cur.size, lat.size)
        val c = cur + List(maxLen - cur.size) { 0 }
        val l = lat + List(maxLen - lat.size) { 0 }
        c.zip(l).fold(0) { cmp, (a, b) -> if (cmp != 0) cmp else a.compareTo(b) } >= 0
    }

    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(icon, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                Spacer(modifier = Modifier.width(8.dp))
                Column(modifier = Modifier.weight(1f)) {
                    Text(title, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                    if (detail != null) {
                        Text(detail, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
                if (currentFW == null || currentFW == "Querying…") {
                    CircularProgressIndicator(modifier = Modifier.size(16.dp), strokeWidth = 2.dp)
                } else if (isUpToDate) {
                    Icon(Icons.Default.CheckCircle, "Up to date", tint = MaterialTheme.colorScheme.primary)
                } else {
                    Text("Update", fontSize = 12.sp, color = MaterialTheme.colorScheme.error)
                }
            }
            Spacer(modifier = Modifier.height(8.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Column {
                    Text("Installed", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(
                        currentFW ?: "Querying…",
                        fontSize = 14.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
                Column(horizontalAlignment = Alignment.End) {
                    Text("Latest", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                    Text(latestFW, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
                }
            }
        }
    }
}

@Composable
private fun MissingRefloatCard(onInstall: () -> Unit) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Warning, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                Spacer(modifier = Modifier.width(8.dp))
                Text("No Refloat Package", fontWeight = FontWeight.Bold)
            }
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                "Refloat is required for onewheel balancing. Install it to continue riding.",
                fontSize = 12.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Spacer(modifier = Modifier.height(8.dp))
            Button(onClick = onInstall, modifier = Modifier.fillMaxWidth()) {
                Text("Install Refloat")
            }
        }
    }
}

// MARK: - Identify Screen

@Composable
private fun IdentifyScreen(engine: NoseDiveEngine, onContinue: () -> Unit, onSkip: () -> Unit) {
    val mainFW by engine.mainFW.collectAsState()
    val refloatInfo by engine.refloatInfo.collectAsState()
    val canDevices by engine.canDevices.collectAsState()
    val guessedBoardType by engine.guessedBoardType.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            Icons.Default.Memory,
            contentDescription = null,
            modifier = Modifier.size(56.dp),
            tint = MaterialTheme.colorScheme.primary
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text("New Board Detected", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Spacer(modifier = Modifier.height(16.dp))

        // Board info card
        val fw = mainFW
        if (fw != null) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    InfoRow("Hardware", fw.hwName)
                    InfoRow("Firmware", fw.versionString)
                    InfoRow("UUID", fw.uuid.take(16) + "…")

                    val ri = refloatInfo
                    if (ri != null) {
                        InfoRow("Package", "${ri.name} ${ri.versionString}")
                    } else if (fw.customConfigCount > 0) {
                        InfoRow("Package", "Detected (querying…)")
                    } else {
                        InfoRow("Package", "None installed")
                    }
                }
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        // Guessed board type
        val guess = guessedBoardType
        if (guess != null) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(Icons.Default.AutoAwesome, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                    Spacer(modifier = Modifier.width(8.dp))
                    Text("Looks like a $guess", fontWeight = FontWeight.Medium)
                }
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        // CAN bus devices
        if (canDevices.isNotEmpty()) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Text("CAN Bus Devices", fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(8.dp))
                    canDevices.forEach { id ->
                        Row(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(vertical = 4.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Icon(
                                iconForDevice(id),
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(24.dp)
                            )
                            Spacer(modifier = Modifier.width(12.dp))
                            Text(nameForDevice(id), fontSize = 14.sp, modifier = Modifier.weight(1f))
                            Icon(
                                Icons.Default.CheckCircle,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.size(20.dp)
                            )
                        }
                    }
                }
            }
            Spacer(modifier = Modifier.height(12.dp))
        }

        Spacer(modifier = Modifier.height(8.dp))
        PrimaryButton("Continue Setup", onClick = onContinue)
        Spacer(modifier = Modifier.height(8.dp))
        TextButton(onClick = onSkip) {
            Text("Skip Setup", color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(label, fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, fontSize = 14.sp, fontFamily = FontFamily.Monospace)
    }
}

private fun iconForDevice(id: Int) = when (id) {
    0 -> Icons.Default.Memory
    10 -> Icons.Default.BatteryChargingFull
    253 -> Icons.Default.Wifi
    else -> Icons.Default.Circle
}

private fun nameForDevice(id: Int) = when (id) {
    0 -> "VESC Motor Controller"
    10 -> "BMS"
    253 -> "VESC Express"
    else -> "Device $id"
}

// MARK: - Refloat Check Screen

@Composable
private fun RefloatCheckScreen(engine: NoseDiveEngine, onContinue: () -> Unit) {
    val refloatInfo by engine.refloatInfo.collectAsState()
    val refloatInstalling by engine.refloatInstalling.collectAsState()
    val refloatInstalled by engine.refloatInstalled.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Icon(
            Icons.Default.Surfing,
            contentDescription = null,
            modifier = Modifier.size(56.dp),
            tint = MaterialTheme.colorScheme.primary
        )
        Spacer(modifier = Modifier.height(8.dp))
        Text("Refloat Package", fontWeight = FontWeight.Bold, fontSize = 20.sp)
        Spacer(modifier = Modifier.height(4.dp))
        Text(
            "Refloat is the balance package that makes your board rideable.",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            fontSize = 14.sp
        )
        Spacer(modifier = Modifier.height(16.dp))

        if (engine.hasRefloat) {
            // Installed
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.CheckCircle, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Refloat Installed", fontWeight = FontWeight.Bold)
                    }
                    val ri = refloatInfo
                    if (ri != null) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column {
                                Text("Version", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                Text(ri.versionString, fontFamily = FontFamily.Monospace)
                            }
                            Column(horizontalAlignment = Alignment.End) {
                                Text("Package", fontSize = 10.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                                Text(ri.name)
                            }
                        }
                    } else {
                        Spacer(modifier = Modifier.height(8.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            CircularProgressIndicator(modifier = Modifier.size(14.dp), strokeWidth = 2.dp)
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("Querying version…", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurfaceVariant)
                        }
                    }
                }
            }
        } else if (refloatInstalling) {
            // Installing
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    CircularProgressIndicator()
                    Spacer(modifier = Modifier.height(12.dp))
                    Text("Installing Refloat…", fontWeight = FontWeight.Bold)
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        "Do not disconnect your board during installation.",
                        fontSize = 12.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
        } else if (refloatInstalled) {
            // Just installed
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.CheckCircle, contentDescription = null, tint = MaterialTheme.colorScheme.primary)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Refloat Installed Successfully", fontWeight = FontWeight.Bold)
                    }
                    val ri = refloatInfo
                    if (ri != null) {
                        Spacer(modifier = Modifier.height(8.dp))
                        Text("Version ${ri.versionString}", fontFamily = FontFamily.Monospace)
                    }
                }
            }
        } else {
            // Not installed
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(modifier = Modifier.padding(16.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Default.Warning, contentDescription = null, tint = MaterialTheme.colorScheme.error)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Refloat Not Installed", fontWeight = FontWeight.Bold)
                    }
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        "Your board does not have Refloat installed. Without it, the board cannot balance. Install Refloat to make your board rideable.",
                        fontSize = 12.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    Spacer(modifier = Modifier.height(12.dp))
                    Button(onClick = { engine.installRefloat() }, modifier = Modifier.fillMaxWidth()) {
                        Icon(Icons.Default.Download, contentDescription = null)
                        Spacer(modifier = Modifier.width(8.dp))
                        Text("Install Refloat")
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(24.dp))
        PrimaryButton("Continue", onClick = onContinue)
    }
}

// MARK: - Complete Screen

@Composable
private fun CompleteScreen(engine: NoseDiveEngine, onDone: () -> Unit) {
    val boardName by engine.activeBoardName.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Spacer(modifier = Modifier.weight(1f))

        Icon(
            Icons.Default.CheckCircle,
            contentDescription = null,
            modifier = Modifier.size(72.dp),
            tint = MaterialTheme.colorScheme.primary
        )
        Spacer(modifier = Modifier.height(16.dp))
        Text("Setup Complete", fontWeight = FontWeight.Bold, fontSize = 22.sp)
        Spacer(modifier = Modifier.height(8.dp))

        val name = boardName
        if (name != null) {
            Text(
                "$name is ready to ride.",
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }

        Spacer(modifier = Modifier.weight(1f))
        PrimaryButton("Done", onClick = onDone)
    }
}

// MARK: - Shared UI

@Composable
private fun PrimaryButton(text: String, onClick: () -> Unit) {
    Button(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(10.dp)
    ) {
        Text(text, fontWeight = FontWeight.Bold, modifier = Modifier.padding(vertical = 4.dp))
    }
}
