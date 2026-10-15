package com.darthdrip.rec

import android.media.MediaCodec
import android.media.MediaFormat
import android.media.MediaMuxer
import android.util.Log
import java.io.FileDescriptor
import java.nio.ByteBuffer

/**
 * Junta video y audio en un MP4. Maneja pausa/reanudar (sin cortes en el archivo final),
 * espera a que existan ambas pistas antes de empezar y nunca deja timestamps desordenados.
 */
class SafeMuxer(fd: FileDescriptor, @Volatile private var expectAudio: Boolean) {

    companion object {
        const val VIDEO = 0
        const val AUDIO = 1
        private const val TAG = "DarthRec"
    }

    private class Sample(val type: Int, val data: ByteArray, val pts: Long, val flags: Int)

    private val muxer = MediaMuxer(fd, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
    private val tracks = intArrayOf(-1, -1)
    private var started = false
    private var finished = false
    private val pending = ArrayDeque<Sample>()

    private var paused = false
    private var pauseStartUs = 0L
    private var pausedTotalUs = 0L
    private var resumeAtUs = 0L
    private var waitKey = true
    private val lastPts = longArrayOf(-1L, -1L)
    private val outInfo = MediaCodec.BufferInfo()
    private var samples = 0

    var onNeedKeyFrame: (() -> Unit)? = null

    private fun nowUs() = System.nanoTime() / 1000

    @Synchronized
    fun addTrack(type: Int, format: MediaFormat) {
        if (started || finished || tracks[type] >= 0) return
        if (type == AUDIO && !expectAudio) return
        tracks[type] = muxer.addTrack(format)
        maybeStart()
    }

    private fun maybeStart() {
        if (started || tracks[VIDEO] < 0) return
        if (expectAudio && tracks[AUDIO] < 0) return
        muxer.start()
        started = true
        while (pending.isNotEmpty()) {
            val s = pending.removeFirst()
            writeRaw(s.type, ByteBuffer.wrap(s.data), 0, s.data.size, s.pts, s.flags)
        }
    }

    @Synchronized
    fun write(type: Int, buf: ByteBuffer, info: MediaCodec.BufferInfo) {
        if (finished || paused || info.size <= 0) return
        if (info.presentationTimeUs < resumeAtUs) return
        if (type == AUDIO && !expectAudio) return
        if (type == VIDEO && waitKey) {
            if ((info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) == 0) return
            waitKey = false
        }
        var pts = info.presentationTimeUs - pausedTotalUs
        if (pts <= lastPts[type]) pts = lastPts[type] + 1
        lastPts[type] = pts
        val flags = info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME

        if (!started) {
            val arr = ByteArray(info.size)
            buf.position(info.offset)
            buf.limit(info.offset + info.size)
            buf.get(arr)
            pending.addLast(Sample(type, arr, pts, flags))
            if (pending.size > 600 && tracks[VIDEO] >= 0 && expectAudio) {
                Log.w(TAG, "El audio no arrancó; se continúa solo con video")
                expectAudio = false
                pending.removeAll { it.type == AUDIO }
                maybeStart()
            }
            return
        }
        writeRaw(type, buf, info.offset, info.size, pts, flags)
    }

    private fun writeRaw(type: Int, buf: ByteBuffer, offset: Int, size: Int, pts: Long, flags: Int) {
        val t = tracks[type]
        if (t < 0) return
        buf.position(offset)
        buf.limit(offset + size)
        outInfo.set(offset, size, pts, flags)
        try {
            muxer.writeSampleData(t, buf, outInfo)
            samples++
        } catch (e: Exception) {
            Log.e(TAG, "writeSampleData", e)
        }
    }

    @Synchronized
    fun pause() {
        if (!paused) {
            paused = true
            pauseStartUs = nowUs()
        }
    }

    @Synchronized
    fun resume() {
        if (!paused) return
        val now = nowUs()
        pausedTotalUs += now - pauseStartUs
        resumeAtUs = now
        paused = false
        waitKey = true
        onNeedKeyFrame?.invoke()
    }

    /** Cierra el archivo. Devuelve true si quedó un video válido. */
    @Synchronized
    fun finish(): Boolean {
        if (finished) return false
        finished = true
        var ok = false
        try {
            if (started) {
                muxer.stop()
                ok = samples > 0
            }
        } catch (e: Exception) {
            Log.e(TAG, "muxer.stop", e)
        }
        try { muxer.release() } catch (_: Exception) {}
        return ok
    }
}
