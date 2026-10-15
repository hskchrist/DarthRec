package com.darthdrip.rec

import android.content.Context
import android.media.MediaFormat

data class RecSettings(
    val gameplay: Boolean = false,
    val scale: Float = 1f,
    /** 0 = el máximo de la pantalla */
    val fps: Int = 0,
    val quality: Quality = Quality.MAX,
    val mime: String = MediaFormat.MIMETYPE_VIDEO_AVC,
    /** 0 = auto, 1 = vertical, 2 = horizontal */
    val orientation: Int = 0,
    val internalAudio: Boolean = true,
    val mic: Boolean = false,
    val facecam: Boolean = false,
    val camSize: Int = 140,
    val camCircle: Boolean = true,
    val countdown: Boolean = true,
)

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("darthrec", Context.MODE_PRIVATE)

    fun load(): RecSettings {
        val d = RecSettings()
        return RecSettings(
            gameplay = sp.getBoolean("gameplay", d.gameplay),
            scale = sp.getFloat("scale", d.scale),
            fps = sp.getInt("fps", d.fps),
            quality = runCatching { Quality.valueOf(sp.getString("quality", d.quality.name) ?: d.quality.name) }
                .getOrDefault(d.quality),
            mime = sp.getString("mime", d.mime) ?: d.mime,
            orientation = sp.getInt("orient", d.orientation),
            internalAudio = sp.getBoolean("ia", d.internalAudio),
            mic = sp.getBoolean("mic", d.mic),
            facecam = sp.getBoolean("cam", d.facecam),
            camSize = sp.getInt("camSize", d.camSize),
            camCircle = sp.getBoolean("camCircle", d.camCircle),
            countdown = sp.getBoolean("cd", d.countdown),
        )
    }

    fun save(s: RecSettings) {
        sp.edit()
            .putBoolean("gameplay", s.gameplay)
            .putFloat("scale", s.scale)
            .putInt("fps", s.fps)
            .putString("quality", s.quality.name)
            .putString("mime", s.mime)
            .putInt("orient", s.orientation)
            .putBoolean("ia", s.internalAudio)
            .putBoolean("mic", s.mic)
            .putBoolean("cam", s.facecam)
            .putInt("camSize", s.camSize)
            .putBoolean("camCircle", s.camCircle)
            .putBoolean("cd", s.countdown)
            .apply()
    }
}
