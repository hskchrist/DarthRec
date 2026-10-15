package com.darthdrip.rec

import android.content.Intent

data class RecordConfig(
    val width: Int,
    val height: Int,
    val fps: Int,
    val bitrate: Int,
    val mime: String,
    val codecName: String,
    val internalAudio: Boolean,
    val mic: Boolean,
    val facecam: Boolean,
    val facecamSizeDp: Int,
    val facecamCircle: Boolean,
    val countdown: Boolean,
) {
    fun writeTo(i: Intent): Intent = i
        .putExtra("w", width)
        .putExtra("h", height)
        .putExtra("fps", fps)
        .putExtra("br", bitrate)
        .putExtra("mime", mime)
        .putExtra("codec", codecName)
        .putExtra("ia", internalAudio)
        .putExtra("mic", mic)
        .putExtra("cam", facecam)
        .putExtra("camSize", facecamSizeDp)
        .putExtra("camCircle", facecamCircle)
        .putExtra("cd", countdown)

    companion object {
        fun from(i: Intent) = RecordConfig(
            width = i.getIntExtra("w", 1080),
            height = i.getIntExtra("h", 1920),
            fps = i.getIntExtra("fps", 60),
            bitrate = i.getIntExtra("br", 20_000_000),
            mime = i.getStringExtra("mime") ?: "video/avc",
            codecName = i.getStringExtra("codec") ?: "",
            internalAudio = i.getBooleanExtra("ia", false),
            mic = i.getBooleanExtra("mic", false),
            facecam = i.getBooleanExtra("cam", false),
            facecamSizeDp = i.getIntExtra("camSize", 140),
            facecamCircle = i.getBooleanExtra("camCircle", true),
            countdown = i.getBooleanExtra("cd", true),
        )
    }
}
