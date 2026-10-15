package com.darthdrip.rec

import android.app.Activity
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.os.Build
import android.util.DisplayMetrics
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Calidad = bits por píxel por fotograma. Más alto = más nitidez y archivos más grandes. */
enum class Quality(val label: String, val avcBpp: Double, val hevcBpp: Double) {
    MAX("Máxima", 0.22, 0.15),
    HIGH("Alta", 0.14, 0.09),
    SAVER("Ahorro", 0.07, 0.05),
}

class Encoder(
    val name: String,
    val mime: String,
    val hardware: Boolean,
    val video: MediaCodecInfo.VideoCapabilities,
) {
    val label: String get() = if (mime == MediaFormat.MIMETYPE_VIDEO_HEVC) "H.265" else "H.264"
    val maxBitrate: Int get() = video.bitrateRange.upper
}

class DeviceInfo(
    val model: String,
    val android: String,
    /** Lado corto y lado largo de la pantalla física, en píxeles. */
    val nativeW: Int,
    val nativeH: Int,
    val maxHz: Int,
    val encoders: List<Encoder>,
)

data class Plan(val width: Int, val height: Int, val fps: Int, val bitrate: Int, val encoder: Encoder)

object DeviceCaps {

    fun load(activity: Activity): DeviceInfo {
        val (w, h) = realSize(activity)
        val maker = Build.MANUFACTURER.replaceFirstChar { it.uppercase() }
        return DeviceInfo(
            model = "$maker ${Build.MODEL}",
            android = "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})",
            nativeW = min(w, h),
            nativeH = max(w, h),
            maxHz = maxRefresh(activity),
            encoders = listOfNotNull(
                best(MediaFormat.MIMETYPE_VIDEO_AVC),
                best(MediaFormat.MIMETYPE_VIDEO_HEVC),
            ),
        )
    }

    @Suppress("DEPRECATION")
    private fun realSize(a: Activity): Pair<Int, Int> =
        if (Build.VERSION.SDK_INT >= 30) {
            val b = a.windowManager.maximumWindowMetrics.bounds
            b.width() to b.height()
        } else {
            val m = DisplayMetrics()
            a.windowManager.defaultDisplay.getRealMetrics(m)
            m.widthPixels to m.heightPixels
        }

    @Suppress("DEPRECATION")
    private fun maxRefresh(a: Activity): Int {
        val d = if (Build.VERSION.SDK_INT >= 30) a.display else a.windowManager.defaultDisplay
        val hz = d?.supportedModes?.maxOfOrNull { it.refreshRate } ?: d?.refreshRate ?: 60f
        return hz.roundToInt().coerceAtLeast(30)
    }

    /** El mejor codificador del celular para ese formato: hardware primero, luego el de mayor bitrate. */
    private fun best(mime: String): Encoder? {
        val infos = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .filter { it.isEncoder && it.supportedTypes.any { t -> t.equals(mime, ignoreCase = true) } }
        val encoders = infos.mapNotNull { info ->
            try {
                val caps = info.getCapabilitiesForType(mime)
                val v = caps.videoCapabilities ?: return@mapNotNull null
                val lower = info.name.lowercase()
                val hw = if (Build.VERSION.SDK_INT >= 29) info.isHardwareAccelerated
                else !(lower.startsWith("omx.google.") || lower.startsWith("c2.android."))
                Encoder(info.name, mime, hw, v)
            } catch (e: Exception) {
                null
            }
        }
        return encoders.sortedWith(
            compareByDescending<Encoder> { it.hardware }.thenByDescending { it.maxBitrate }
        ).firstOrNull()
    }

    /**
     * Elige la combinación más alta que el codificador de ESTE celular soporta de verdad.
     * Primero mantiene los fps (fluidez) bajando un poco la resolución si hace falta;
     * si aun así no se puede, baja los fps.
     */
    fun plan(info: DeviceInfo, enc: Encoder, scale: Float, wantFps: Int, quality: Quality, landscape: Boolean): Plan {
        val v = enc.video
        val fpsList = listOf(wantFps, 144, 120, 90, 60, 50, 30).filter { it <= wantFps }.distinct()
        val steps = listOf(1f, 0.9f, 0.8f, 0.75f, 0.7f, 0.6f, 0.5f, 0.4f, 0.33f)

        fun dims(s: Float): Pair<Int, Int> {
            var w = (info.nativeW * scale * s).toInt()
            var h = (info.nativeH * scale * s).toInt()
            if (landscape) { val t = w; w = h; h = t }
            return align(w, v.widthAlignment) to align(h, v.heightAlignment)
        }

        fun ok(w: Int, h: Int, fps: Int): Boolean = try {
            w > 0 && h > 0 && v.isSizeSupported(w, h) && v.areSizeAndRateSupported(w, h, fps.toDouble())
        } catch (e: Exception) {
            false
        }

        for (fps in fpsList) for (s in steps.filter { it >= 0.7f }) {
            val (w, h) = dims(s)
            if (ok(w, h, fps)) return make(w, h, fps, enc, quality)
        }
        for (fps in fpsList) for (s in steps) {
            val (w, h) = dims(s)
            if (ok(w, h, fps)) return make(w, h, fps, enc, quality)
        }
        val (w, h) = dims(0.5f)
        return make(w, h, 30, enc, quality)
    }

    private fun align(x: Int, a: Int): Int {
        val al = max(2, a)
        return (x / al) * al
    }

    private fun make(w: Int, h: Int, fps: Int, enc: Encoder, q: Quality): Plan {
        val bpp = if (enc.mime == MediaFormat.MIMETYPE_VIDEO_HEVC) q.hevcBpp else q.avcBpp
        val upper = min(enc.maxBitrate, 200_000_000).coerceAtLeast(2_000_000)
        val br = (w.toDouble() * h * fps * bpp).toLong().coerceIn(2_000_000L, upper.toLong()).toInt()
        return Plan(w, h, fps, br, enc)
    }
}
