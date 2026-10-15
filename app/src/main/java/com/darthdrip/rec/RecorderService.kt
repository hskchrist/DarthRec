package com.darthdrip.rec

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.projection.MediaProjection
import android.media.projection.MediaProjectionManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import androidx.core.content.IntentCompat
import androidx.lifecycle.LifecycleService
import com.darthdrip.rec.RecorderState.Status

class RecorderService : LifecycleService() {

    companion object {
        const val ACTION_START = "com.darthdrip.rec.START"
        const val ACTION_STOP = "com.darthdrip.rec.STOP"
        const val ACTION_PAUSE = "com.darthdrip.rec.PAUSE"
        const val ACTION_RESUME = "com.darthdrip.rec.RESUME"
        const val EXTRA_CODE = "code"
        const val EXTRA_DATA = "data"
        private const val CH_REC = "rec"
        private const val CH_DONE = "done"
        private const val NOTIF_REC = 7
        private const val NOTIF_DONE = 8
        private const val TAG = "DarthRec"
    }

    private val main = Handler(Looper.getMainLooper())
    private var projection: MediaProjection? = null
    private var recorder: ScreenRecorder? = null
    private var output: Storage.Output? = null
    private var facecam: FacecamOverlay? = null
    private var active = false
    private var stopping = false

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        super.onStartCommand(intent, flags, startId)
        when (intent?.action) {
            ACTION_START -> if (!active) start(intent) else Log.w(TAG, "Ya está grabando")
            ACTION_STOP -> if (active) stopRecording() else stopSelf()
            ACTION_PAUSE -> pause()
            ACTION_RESUME -> resume()
        }
        return START_NOT_STICKY
    }

    private fun granted(p: String) = ContextCompat.checkSelfPermission(this, p) == PackageManager.PERMISSION_GRANTED

    private fun start(intent: Intent) {
        val cfg = RecordConfig.from(intent)
        createChannels()
        val hasCam = granted(Manifest.permission.CAMERA)
        val hasMic = granted(Manifest.permission.RECORD_AUDIO)

        var type = 0
        if (Build.VERSION.SDK_INT >= 29) type = ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROJECTION
        if (Build.VERSION.SDK_INT >= 30) {
            if (cfg.facecam && hasCam) type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
            if ((cfg.mic || cfg.internalAudio) && hasMic) type = type or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
        }
        try {
            ServiceCompat.startForeground(this, NOTIF_REC, buildNotif(if (cfg.countdown) "Comienza en 3…" else "Grabando", false), type)
        } catch (e: Exception) {
            Log.e(TAG, "startForeground", e)
            RecorderState.message.value = "No se pudo iniciar la grabación: ${e.message}"
            RecorderState.status.value = Status.IDLE
            stopSelf()
            return
        }
        active = true
        stopping = false
        RecorderState.resetClock()
        RecorderState.message.value = null

        try {
            val code = intent.getIntExtra(EXTRA_CODE, 0)
            val data = IntentCompat.getParcelableExtra(intent, EXTRA_DATA, Intent::class.java)
                ?: throw IllegalStateException("faltan los datos de captura")
            val mpm = getSystemService(MediaProjectionManager::class.java)
            val proj = mpm.getMediaProjection(code, data)
                ?: throw IllegalStateException("permiso de captura no válido")
            proj.registerCallback(object : MediaProjection.Callback() {
                override fun onStop() {
                    main.post { stopRecording() }
                }
            }, main)
            projection = proj

            if (cfg.facecam && hasCam && Settings.canDrawOverlays(this)) {
                facecam = FacecamOverlay(this, this, cfg.facecamSizeDp, cfg.facecamCircle).also { it.show() }
            }

            val out = Storage.create(this)
            output = out
            val rec = ScreenRecorder(proj, cfg, resources.displayMetrics.densityDpi, out.pfd.fileDescriptor) { msg ->
                main.post {
                    RecorderState.message.value = msg
                    stopRecording()
                }
            }
            recorder = rec
            rec.warning?.let { RecorderState.message.value = it }
            rec.start(paused = cfg.countdown)

            if (cfg.countdown) {
                RecorderState.status.value = Status.COUNTDOWN
                countdown(3)
            } else {
                beginRecording()
            }
        } catch (e: Exception) {
            Log.e(TAG, "start", e)
            RecorderState.message.value = "No se pudo grabar: ${e.message}"
            stopRecording()
        }
    }

    private fun countdown(n: Int) {
        if (stopping) return
        if (n <= 0) { beginRecording(); return }
        updateNotif("Comienza en $n…", false)
        main.postDelayed({ countdown(n - 1) }, 1000)
    }

    private fun beginRecording() {
        recorder?.resume()
        RecorderState.runClock()
        RecorderState.status.value = Status.RECORDING
        updateNotif("Grabando", false)
    }

    private fun pause() {
        val r = recorder ?: return
        if (RecorderState.status.value != Status.RECORDING) return
        r.pause()
        RecorderState.pauseClock()
        RecorderState.status.value = Status.PAUSED
        updateNotif("En pausa", true)
    }

    private fun resume() {
        val r = recorder ?: return
        if (RecorderState.status.value != Status.PAUSED) return
        r.resume()
        RecorderState.runClock()
        RecorderState.status.value = Status.RECORDING
        updateNotif("Grabando", false)
    }

    private fun stopRecording() {
        if (stopping) return
        stopping = true
        main.removeCallbacksAndMessages(null)
        RecorderState.pauseClock()
        RecorderState.status.value = Status.SAVING
        updateNotif("Guardando video…", false)
        facecam?.hide()
        facecam = null

        val rec = recorder
        val out = output
        val proj = projection
        recorder = null
        output = null
        projection = null

        Thread {
            val ok = try { rec?.stop() ?: false } catch (e: Exception) { Log.e(TAG, "stop", e); false }
            try { proj?.stop() } catch (_: Exception) {}
            val finish: (Uri?) -> Unit = { uri -> main.post { done(uri, ok) } }
            if (out != null) {
                try { out.complete(this, ok, finish) } catch (e: Exception) { Log.e(TAG, "complete", e); finish(null) }
            } else {
                finish(null)
            }
        }.start()
    }

    private fun done(uri: Uri?, ok: Boolean) {
        RecorderState.resetClock()
        if (ok) {
            RecorderState.lastUri.value = uri
            if (RecorderState.message.value == null) RecorderState.message.value = "Video guardado en Películas/DarthRec"
            notifySaved(uri)
        } else if (RecorderState.message.value == null) {
            RecorderState.message.value = "La grabación quedó vacía y se descartó"
        }
        RecorderState.status.value = Status.IDLE
        active = false
        stopping = false
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    override fun onDestroy() {
        if (active && !stopping) stopRecording()
        super.onDestroy()
    }

    // ---------- Notificaciones ----------

    private fun createChannels() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CH_REC, "Grabación en curso", NotificationManager.IMPORTANCE_LOW))
        nm.createNotificationChannel(NotificationChannel(CH_DONE, "Videos guardados", NotificationManager.IMPORTANCE_DEFAULT))
    }

    private val piFlags = PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT

    private fun buildNotif(text: String, paused: Boolean): Notification {
        fun svc(action: String, code: Int) = PendingIntent.getService(
            this, code, Intent(this, RecorderService::class.java).setAction(action), piFlags,
        )
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), piFlags,
        )
        val b = NotificationCompat.Builder(this, CH_REC)
            .setSmallIcon(R.drawable.ic_stat_rec)
            .setContentTitle("DarthRec")
            .setContentText(text)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setContentIntent(open)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .setForegroundServiceBehavior(NotificationCompat.FOREGROUND_SERVICE_IMMEDIATE)
        if (active && !stopping) {
            b.addAction(0, if (paused) "Reanudar" else "Pausar", svc(if (paused) ACTION_RESUME else ACTION_PAUSE, 2))
            b.addAction(0, "Detener", svc(ACTION_STOP, 1))
        } else if (!stopping) {
            b.addAction(0, "Detener", svc(ACTION_STOP, 1))
        }
        if (RecorderState.status.value == Status.RECORDING) {
            b.setUsesChronometer(true)
                .setShowWhen(true)
                .setWhen(System.currentTimeMillis() - RecorderState.elapsedMs())
        }
        return b.build()
    }

    private fun updateNotif(text: String, paused: Boolean) {
        try {
            getSystemService(NotificationManager::class.java).notify(NOTIF_REC, buildNotif(text, paused))
        } catch (_: Exception) {}
    }

    private fun notifySaved(uri: Uri?) {
        val nb = NotificationCompat.Builder(this, CH_DONE)
            .setSmallIcon(R.drawable.ic_stat_rec)
            .setContentTitle("Video guardado")
            .setContentText("Películas/DarthRec · toca para verlo")
            .setAutoCancel(true)
        if (uri != null) {
            val view = Intent(Intent.ACTION_VIEW)
                .setDataAndType(uri, "video/mp4")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
            nb.setContentIntent(PendingIntent.getActivity(this, 5, view, piFlags))
        }
        try {
            getSystemService(NotificationManager::class.java).notify(NOTIF_DONE, nb.build())
        } catch (_: Exception) {}
    }
}
