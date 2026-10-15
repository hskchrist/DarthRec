package com.darthdrip.rec

import android.hardware.display.DisplayManager
import android.hardware.display.VirtualDisplay
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.media.projection.MediaProjection
import android.os.Bundle
import android.os.Process
import android.util.Log
import android.view.Surface
import java.io.FileDescriptor

/**
 * Captura la pantalla directo al codificador de hardware (sin copias intermedias),
 * con prioridad de tiempo real para máxima fluidez.
 */
class ScreenRecorder(
    private val projection: MediaProjection,
    private val cfg: RecordConfig,
    private val densityDpi: Int,
    fd: FileDescriptor,
    private val onFatal: (String) -> Unit,
) {
    companion object { private const val TAG = "DarthRec" }

    private var audio: AudioEncoder? = null
    private val mux: SafeMuxer
    private val video: MediaCodec
    private val surface: Surface
    private var display: VirtualDisplay? = null
    private var videoThread: Thread? = null
    @Volatile private var stopRequested = false

    var warning: String? = null
        private set

    init {
        if (cfg.internalAudio || cfg.mic) {
            val a = AudioEncoder(projection, cfg.internalAudio, cfg.mic)
            try {
                a.prepare()
                audio = a
                warning = a.warning
            } catch (e: Exception) {
                Log.w(TAG, "audio", e)
                warning = "Se grabará sin audio: ${e.message}"
            }
        }
        mux = SafeMuxer(fd, expectAudio = audio != null)
        val created = try {
            val c = createVideoCodec()
            c to c.createInputSurface()
        } catch (e: Exception) {
            audio?.stop()
            mux.finish()
            throw e
        }
        video = created.first
        surface = created.second
        mux.onNeedKeyFrame = { requestKeyFrame() }
    }

    fun start(paused: Boolean) {
        if (paused) mux.pause()
        video.start()
        display = projection.createVirtualDisplay(
            "DarthRec", cfg.width, cfg.height, densityDpi,
            DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
            surface, null, null,
        )
        videoThread = Thread({ drainVideo() }, "rec-video").also { it.start() }
        audio?.start(mux)
    }

    fun pause() = mux.pause()
    fun resume() = mux.resume()

    /** Detiene todo y cierra el MP4. Devuelve true si el video quedó bien guardado. */
    fun stop(): Boolean {
        stopRequested = true
        audio?.stop()
        try { video.signalEndOfInputStream() } catch (_: Exception) {}
        videoThread?.join(5000)
        try { display?.release() } catch (_: Exception) {}
        try { video.stop() } catch (_: Exception) {}
        try { video.release() } catch (_: Exception) {}
        try { surface.release() } catch (_: Exception) {}
        return mux.finish()
    }

    private fun requestKeyFrame() {
        try {
            video.setParameters(Bundle().apply { putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0) })
        } catch (_: Exception) {}
    }

    private fun drainVideo() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_DISPLAY)
        val info = MediaCodec.BufferInfo()
        var deadline = 0L
        try {
            while (true) {
                if (stopRequested && deadline == 0L) deadline = System.nanoTime() + 3_000_000_000L
                if (deadline != 0L && System.nanoTime() > deadline) break
                val idx = video.dequeueOutputBuffer(info, 10_000L)
                when {
                    idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> mux.addTrack(SafeMuxer.VIDEO, video.outputFormat)
                    idx >= 0 -> {
                        val buf = video.getOutputBuffer(idx)
                        if (buf != null && info.size > 0 && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                            mux.write(SafeMuxer.VIDEO, buf, info)
                        }
                        video.releaseOutputBuffer(idx, false)
                        if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "video", e)
            if (!stopRequested) onFatal("Error del codificador de video: ${e.message}")
        }
    }

    private fun createVideoCodec(): MediaCodec {
        var last: Exception? = null
        for (f in buildFormats()) {
            val c = try {
                MediaCodec.createByCodecName(cfg.codecName)
            } catch (e: Exception) {
                MediaCodec.createEncoderByType(cfg.mime)
            }
            try {
                c.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
                Log.i(TAG, "Codificador listo: ${c.name} $f")
                return c
            } catch (e: Exception) {
                last = e
                c.release()
            }
        }
        throw IllegalStateException("El codificador no aceptó la configuración", last)
    }

    /** De la configuración más exigente a la más compatible; se usa la primera que acepte el celular. */
    private fun buildFormats(): List<MediaFormat> {
        val caps = MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos
            .firstOrNull { it.name == cfg.codecName }
            ?.let { try { it.getCapabilitiesForType(cfg.mime) } catch (_: Exception) { null } }
        val vbr = caps?.encoderCapabilities
            ?.isBitrateModeSupported(MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR) == true
        val wantProfile = if (cfg.mime == MediaFormat.MIMETYPE_VIDEO_HEVC)
            MediaCodecInfo.CodecProfileLevel.HEVCProfileMain
        else MediaCodecInfo.CodecProfileLevel.AVCProfileHigh
        val pl = caps?.profileLevels?.filter { it.profile == wantProfile }?.maxByOrNull { it.level }

        fun base(br: Int, fps: Int) = MediaFormat.createVideoFormat(cfg.mime, cfg.width, cfg.height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, br)
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
            // Si la pantalla no cambia, repite el último cuadro para no "congelar" el video.
            setLong(MediaFormat.KEY_REPEAT_PREVIOUS_FRAME_AFTER, 100_000L)
        }

        return listOf(
            base(cfg.bitrate, cfg.fps).apply {
                if (vbr) setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                setInteger(MediaFormat.KEY_PRIORITY, 0) // tiempo real
                setInteger(MediaFormat.KEY_OPERATING_RATE, cfg.fps)
                if (pl != null) {
                    setInteger(MediaFormat.KEY_PROFILE, pl.profile)
                    setInteger(MediaFormat.KEY_LEVEL, pl.level)
                }
            },
            base(cfg.bitrate, cfg.fps).apply {
                if (vbr) setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
                setInteger(MediaFormat.KEY_PRIORITY, 0)
            },
            base(cfg.bitrate, cfg.fps),
            base(cfg.bitrate / 2, minOf(cfg.fps, 60)),
        )
    }
}
