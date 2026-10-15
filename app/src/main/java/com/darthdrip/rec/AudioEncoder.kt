package com.darthdrip.rec

import android.annotation.SuppressLint
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioPlaybackCaptureConfiguration
import android.media.AudioRecord
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.media.MediaRecorder
import android.media.projection.MediaProjection
import android.os.Build
import android.os.Process
import android.util.Log
import java.nio.ByteOrder
import kotlin.math.max
import kotlin.math.min

/**
 * Audio interno (juego/sistema, Android 10+) y/o micrófono, mezclados en una sola pista AAC
 * estéreo 48 kHz a 256 kbps.
 */
class AudioEncoder(
    private val projection: MediaProjection,
    private val wantInternal: Boolean,
    private val wantMic: Boolean,
) {
    companion object {
        const val RATE = 48_000
        private const val FRAMES = 1024
        private const val TAG = "DarthRec"
    }

    private var internalRec: AudioRecord? = null
    private var micRec: AudioRecord? = null
    private var codec: MediaCodec? = null
    private var mux: SafeMuxer? = null
    private var thread: Thread? = null
    @Volatile private var running = false
    private val info = MediaCodec.BufferInfo()

    var warning: String? = null
        private set

    @SuppressLint("MissingPermission")
    fun prepare() {
        if (wantInternal && Build.VERSION.SDK_INT >= 29) {
            try {
                val fmt = AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(RATE)
                    .setChannelMask(AudioFormat.CHANNEL_IN_STEREO)
                    .build()
                val cap = AudioPlaybackCaptureConfiguration.Builder(projection)
                    .addMatchingUsage(AudioAttributes.USAGE_MEDIA)
                    .addMatchingUsage(AudioAttributes.USAGE_GAME)
                    .addMatchingUsage(AudioAttributes.USAGE_UNKNOWN)
                    .build()
                val minBuf = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT)
                val r = AudioRecord.Builder()
                    .setAudioFormat(fmt)
                    .setBufferSizeInBytes(max(minBuf * 4, 64 * 1024))
                    .setAudioPlaybackCaptureConfig(cap)
                    .build()
                if (r.state == AudioRecord.STATE_INITIALIZED) internalRec = r
                else { r.release(); warning = "No se pudo capturar el audio interno" }
            } catch (e: Exception) {
                warning = "Audio interno no disponible (${e.message})"
            }
        }
        if (wantMic) {
            try {
                val minBuf = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
                val r = AudioRecord(
                    MediaRecorder.AudioSource.MIC, RATE,
                    AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT,
                    max(minBuf * 4, 32 * 1024),
                )
                if (r.state == AudioRecord.STATE_INITIALIZED) micRec = r
                else { r.release(); warning = "No se pudo abrir el micrófono" }
            } catch (e: Exception) {
                warning = "Micrófono no disponible (${e.message})"
            }
        }
        if (internalRec == null && micRec == null) {
            throw IllegalStateException(warning ?: "sin fuentes de audio")
        }
        try {
            val f = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, RATE, 2).apply {
                setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
                setInteger(MediaFormat.KEY_BIT_RATE, 256_000)
                setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16 * 1024)
            }
            val c = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
            c.configure(f, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            codec = c
        } catch (e: Exception) {
            release()
            throw e
        }
    }

    fun start(m: SafeMuxer) {
        mux = m
        codec?.start()
        running = true
        thread = Thread({ loop() }, "rec-audio").also { it.start() }
    }

    fun stop() {
        running = false
        thread?.join(4000)
        release()
    }

    private fun release() {
        internalRec?.let { try { it.stop() } catch (_: Exception) {}; it.release() }
        micRec?.let { try { it.stop() } catch (_: Exception) {}; it.release() }
        codec?.let { try { it.stop() } catch (_: Exception) {}; try { it.release() } catch (_: Exception) {} }
        internalRec = null
        micRec = null
        codec = null
    }

    private fun loop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
        val c = codec ?: return
        val ir = internalRec
        val mr = micRec
        try {
            ir?.startRecording()
            mr?.startRecording()
        } catch (e: Exception) {
            Log.e(TAG, "startRecording", e)
        }
        val pcm = ShortArray(FRAMES * 2)
        val micPcm = ShortArray(FRAMES)
        var startUs = -1L
        var total = 0L
        try {
            while (running) {
                var n: Int
                if (ir != null) {
                    val r = ir.read(pcm, 0, pcm.size, AudioRecord.READ_BLOCKING)
                    if (r <= 0) { Thread.sleep(5); continue }
                    n = r - (r % 2)
                } else {
                    pcm.fill(0.toShort())
                    n = pcm.size
                }
                if (mr != null) {
                    val want = n / 2
                    var got = 0
                    while (got < want && running) {
                        val r = mr.read(micPcm, got, want - got, AudioRecord.READ_BLOCKING)
                        if (r <= 0) break
                        got += r
                    }
                    for (i in 0 until got) {
                        val m = micPcm[i].toInt()
                        pcm[2 * i] = clip(pcm[2 * i] + m)
                        pcm[2 * i + 1] = clip(pcm[2 * i + 1] + m)
                    }
                }
                if (n == 0) continue
                val frames = n / 2
                if (startUs < 0) startUs = System.nanoTime() / 1000 - frames * 1_000_000L / RATE
                val pts = startUs + total * 1_000_000L / RATE
                total += frames
                feed(c, pcm, n, pts)
                drain(c, false)
            }
            val idx = c.dequeueInputBuffer(20_000)
            if (idx >= 0) {
                val endPts = if (startUs < 0) 0L else startUs + total * 1_000_000L / RATE
                c.queueInputBuffer(idx, 0, 0, endPts, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
            }
            drain(c, true)
        } catch (e: Exception) {
            Log.e(TAG, "audio loop", e)
        }
    }

    private fun clip(v: Int): Short = v.coerceIn(-32768, 32767).toShort()

    private fun feed(c: MediaCodec, pcm: ShortArray, n: Int, pts: Long) {
        var off = 0
        var p = pts
        var tries = 0
        while (off < n && tries < 50) {
            val idx = c.dequeueInputBuffer(5_000)
            if (idx < 0) { tries++; drain(c, false); continue }
            val buf = c.getInputBuffer(idx)
            if (buf == null) { c.queueInputBuffer(idx, 0, 0, p, 0); continue }
            buf.clear()
            val count = (min(n - off, buf.capacity() / 2) / 2) * 2
            if (count <= 0) { c.queueInputBuffer(idx, 0, 0, p, 0); break }
            buf.order(ByteOrder.nativeOrder()).asShortBuffer().put(pcm, off, count)
            c.queueInputBuffer(idx, 0, count * 2, p, 0)
            off += count
            p += (count / 2) * 1_000_000L / RATE
        }
    }

    private fun drain(c: MediaCodec, untilEos: Boolean) {
        val deadline = System.nanoTime() + 2_000_000_000L
        while (true) {
            val idx = c.dequeueOutputBuffer(info, if (untilEos) 10_000L else 0L)
            when {
                idx == MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    if (!untilEos || System.nanoTime() > deadline) return
                }
                idx == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> mux?.addTrack(SafeMuxer.AUDIO, c.outputFormat)
                idx >= 0 -> {
                    val b = c.getOutputBuffer(idx)
                    if (b != null && info.size > 0 && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                        mux?.write(SafeMuxer.AUDIO, b, info)
                    }
                    c.releaseOutputBuffer(idx, false)
                    if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) return
                }
            }
        }
    }
}
