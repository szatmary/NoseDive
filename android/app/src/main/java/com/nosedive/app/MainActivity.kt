package com.nosedive.app

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import com.nosedive.app.engine.NoseDiveEngine
import com.nosedive.app.ui.MainScreen

class MainActivity : ComponentActivity() {

    private lateinit var engine: NoseDiveEngine

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        engine = NoseDiveEngine.getInstance(this)

        setContent {
            MaterialTheme {
                MainScreen(engine)
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isFinishing) {
            engine.destroy()
        }
    }
}
