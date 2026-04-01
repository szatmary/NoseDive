package com.nosedive.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import com.nosedive.app.ble.BLEService
import com.nosedive.app.engine.NoseDiveEngine
import com.nosedive.app.ui.MainScreen

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val engine = NoseDiveEngine.getInstance(this)
        val bleService = BLEService(applicationContext)

        // Wire BLE callbacks into the engine
        engine.bleService = bleService
        bleService.onPayloadReceived = { data -> engine.handlePayload(data) }
        bleService.onConnected = { engine.onConnected() }
        bleService.onDisconnected = { engine.onDisconnected() }

        setContent {
            MaterialTheme {
                MainScreen(engine, bleService)
            }
        }
    }
}
