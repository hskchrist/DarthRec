package com.darthdrip.rec

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.media.projection.MediaProjectionConfig
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.core.content.ContextCompat

class MainActivity : ComponentActivity() {

    private lateinit var info: DeviceInfo
    private lateinit var prefs: Prefs
    private var pending: RecordConfig? = null
    private var askedPerms = false
    private var askedOverlay = false

    private val permLauncher = registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        continueFlow()
    }
    private val overlayLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
        continueFlow()
    }
    private val captureLauncher = registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { res ->
        val cfg = pending
        val data = res.data
        pending = null
        if (res.resultCode == RESULT_OK && data != null && cfg != null) launchService(res.resultCode, data, cfg)
        else toast("Grabación cancelada")
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        info = DeviceCaps.load(this)
        prefs = Prefs(this)
        val landscapeNow = resources.configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
        setContent {
            DarthTheme {
                RecorderScreen(
                    info = info,
                    initial = prefs.load(),
                    isLandscapeNow = landscapeNow,
                    onSettingsChange = { prefs.save(it) },
                    onRecord = { s, plan -> startFlow(s, plan) },
                    onPause = { sendAction(RecorderService.ACTION_PAUSE) },
                    onResume = { sendAction(RecorderService.ACTION_RESUME) },
                    onStop = { sendAction(RecorderService.ACTION_STOP) },
                    onOpen = { openVideo(it) },
                )
            }
        }
    }

    private fun granted(p: String) = ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED

    private fun startFlow(s: RecSettings, plan: Plan) {
        pending = RecordConfig(
            width = plan.width,
            height = plan.height,
            fps = plan.fps,
            bitrate = plan.bitrate,
            mime = plan.encoder.mime,
            codecName = plan.encoder.name,
            internalAudio = s.internalAudio && Build.VERSION.SDK_INT >= 29,
            mic = s.mic,
            facecam = s.facecam,
            facecamSizeDp = s.camSize,
            facecamCircle = s.camCircle,
            countdown = s.countdown,
        )
        askedPerms = false
        askedOverlay = false
        continueFlow()
    }

    /** Pide solo los permisos que hacen falta, en orden, y al final la captura de pantalla. */
    private fun continueFlow() {
        var cfg = pending ?: return

        val need = mutableListOf<String>()
        if ((cfg.mic || cfg.internalAudio) && !granted(Manifest.permission.RECORD_AUDIO)) need += Manifest.permission.RECORD_AUDIO
        if (cfg.facecam && !granted(Manifest.permission.CAMERA)) need += Manifest.permission.CAMERA
        if (Build.VERSION.SDK_INT >= 33 && !granted(Manifest.permission.POST_NOTIFICATIONS)) need += Manifest.permission.POST_NOTIFICATIONS
        if (Build.VERSION.SDK_INT <= 28 && !granted(Manifest.permission.WRITE_EXTERNAL_STORAGE)) need += Manifest.permission.WRITE_EXTERNAL_STORAGE
        if (need.isNotEmpty() && !askedPerms) {
            askedPerms = true
            permLauncher.launch(need.toTypedArray())
            return
        }

        if (!granted(Manifest.permission.RECORD_AUDIO) && (cfg.mic || cfg.internalAudio)) {
            toast("Sin permiso de audio: se grabará sin sonido")
            cfg = cfg.copy(mic = false, internalAudio = false)
        }
        if (!granted(Manifest.permission.CAMERA) && cfg.facecam) {
            toast("Sin permiso de cámara: se grabará sin tu cara")
            cfg = cfg.copy(facecam = false)
        }
        if (Build.VERSION.SDK_INT <= 28 && !granted(Manifest.permission.WRITE_EXTERNAL_STORAGE)) {
            toast("Se necesita el permiso de almacenamiento para guardar videos")
            pending = null
            return
        }
        if (cfg.facecam && !Settings.canDrawOverlays(this)) {
            if (!askedOverlay) {
                askedOverlay = true
                pending = cfg
                toast("Activa \"Mostrar sobre otras apps\" para DarthRec y vuelve")
                overlayLauncher.launch(
                    Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName"))
                )
                return
            }
            toast("Sin permiso de superposición: se grabará sin tu cara")
            cfg = cfg.copy(facecam = false)
        }
        pending = cfg

        val mpm = getSystemService(MediaProjectionManager::class.java)
        val captureIntent = if (Build.VERSION.SDK_INT >= 34) {
            // Pantalla completa siempre (así la burbuja de cámara también queda en el video).
            mpm.createScreenCaptureIntent(MediaProjectionConfig.createConfigForDefaultDisplay())
        } else {
            mpm.createScreenCaptureIntent()
        }
        captureLauncher.launch(captureIntent)
    }

    private fun launchService(code: Int, data: Intent, cfg: RecordConfig) {
        val i = Intent(this, RecorderService::class.java)
            .setAction(RecorderService.ACTION_START)
            .putExtra(RecorderService.EXTRA_CODE, code)
            .putExtra(RecorderService.EXTRA_DATA, data)
        cfg.writeTo(i)
        ContextCompat.startForegroundService(this, i)
        // Volvemos al inicio para que abras tu juego (con una pequeña espera para que el servicio arranque).
        Handler(Looper.getMainLooper()).postDelayed({ moveTaskToBack(true) }, 600)
    }

    private fun sendAction(action: String) {
        try {
            startService(Intent(this, RecorderService::class.java).setAction(action))
        } catch (e: Exception) {
            toast("No se pudo: ${e.message}")
        }
    }

    private fun openVideo(uri: Uri) {
        try {
            startActivity(
                Intent(Intent.ACTION_VIEW)
                    .setDataAndType(uri, "video/mp4")
                    .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            )
        } catch (e: Exception) {
            toast("No hay app para abrir el video")
        }
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
}
