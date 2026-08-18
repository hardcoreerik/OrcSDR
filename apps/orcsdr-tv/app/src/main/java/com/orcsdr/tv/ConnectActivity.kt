package com.orcsdr.tv

import android.content.Context
import android.content.Intent
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class ConnectActivity : AppCompatActivity() {
    private lateinit var host: EditText
    private lateinit var found: LinearLayout
    private lateinit var scanStatus: TextView
    private var nsd: NsdManager? = null
    private var discovery: NsdManager.DiscoveryListener? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_connect)
        host = findViewById(R.id.host)
        found = findViewById(R.id.found)
        scanStatus = findViewById(R.id.scan_status)

        val prefs = getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        host.setText(prefs.getString(KEY_HOST, ""))

        buildPad(findViewById(R.id.pad))
        findViewById<Button>(R.id.connect).setOnClickListener { openHost(host.text.toString()) }
        startDiscovery()
    }

    override fun onDestroy() {
        stopDiscovery()
        super.onDestroy()
    }

    private fun buildPad(root: LinearLayout) {
        val rows = arrayOf(
            arrayOf("1", "2", "3"),
            arrayOf("4", "5", "6"),
            arrayOf("7", "8", "9"),
            arrayOf(".", "0", "DEL"),
        )
        for (row in rows) {
            val line = LinearLayout(this)
            line.orientation = LinearLayout.HORIZONTAL
            for (label in row) {
                val button = Button(this)
                button.text = label
                button.textSize = 22f
                button.isFocusable = true
                val params = LinearLayout.LayoutParams(140, 88)
                params.setMargins(8, 8, 8, 8)
                button.layoutParams = params
                button.setOnClickListener {
                    if (label == "DEL") {
                        val text = host.text
                        if (text.isNotEmpty()) host.setText(text.substring(0, text.length - 1))
                        host.setSelection(host.text.length)
                    } else {
                        host.append(label)
                    }
                }
                line.addView(button)
            }
            root.addView(line)
        }
    }

    private fun openHost(raw: String) {
        val cleaned = raw.trim().removePrefix("http://").removePrefix("https://").trimEnd('/')
        if (cleaned.isEmpty()) return
        getSharedPreferences(PREFS, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_HOST, cleaned)
            .apply()
        startActivity(Intent(this, ConsoleActivity::class.java).putExtra(EXTRA_HOST, cleaned))
    }

    private fun startDiscovery() {
        val manager = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return
        nsd = manager
        val listener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {
                runOnUiThread { scanStatus.text = getString(R.string.scanning) }
            }

            override fun onServiceFound(service: NsdServiceInfo) {
                val name = service.serviceName ?: return
                if (!name.contains("orcsdr", ignoreCase = true) &&
                    !name.contains("OrcSDR", ignoreCase = true)
                ) return
                manager.resolveService(
                    service,
                    object : NsdManager.ResolveListener {
                        override fun onResolveFailed(info: NsdServiceInfo, errorCode: Int) {}

                        override fun onServiceResolved(info: NsdServiceInfo) {
                            val address = info.host?.hostAddress ?: return
                            runOnUiThread { addFound(address) }
                        }
                    },
                )
            }

            override fun onServiceLost(service: NsdServiceInfo) {}
            override fun onDiscoveryStopped(serviceType: String) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                runOnUiThread { scanStatus.text = "Discovery unavailable; type the Tab5 IP." }
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }
        discovery = listener
        try {
            manager.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, listener)
        } catch (_: RuntimeException) {
            scanStatus.text = "Discovery unavailable; type the Tab5 IP."
        }
    }

    private fun addFound(address: String) {
        for (i in 0 until found.childCount) {
            val child = found.getChildAt(i) as? Button ?: continue
            if (child.tag == address) return
        }
        scanStatus.text = "Found OrcSDR on the LAN"
        val button = Button(this)
        button.text = "http://$address/"
        button.tag = address
        button.textSize = 20f
        button.isFocusable = true
        button.setOnClickListener { openHost(address) }
        found.addView(button)
    }

    private fun stopDiscovery() {
        val listener = discovery ?: return
        try {
            nsd?.stopServiceDiscovery(listener)
        } catch (_: RuntimeException) {
        }
        discovery = null
    }

    companion object {
        const val PREFS = "orcsdr_tv"
        const val KEY_HOST = "host"
        const val EXTRA_HOST = "host"
    }
}
