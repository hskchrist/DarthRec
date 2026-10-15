package com.darthdrip.rec

import android.net.Uri
import android.os.SystemClock
import kotlinx.coroutines.flow.MutableStateFlow

/** Estado compartido entre el servicio de grabación y la pantalla. */
object RecorderState {
    enum class Status { IDLE, COUNTDOWN, RECORDING, PAUSED, SAVING }

    val status = MutableStateFlow(Status.IDLE)
    val message = MutableStateFlow<String?>(null)
    val lastUri = MutableStateFlow<Uri?>(null)

    @Volatile private var accumulated = 0L
    @Volatile private var segmentStart = 0L

    fun resetClock() { accumulated = 0L; segmentStart = 0L }
    fun runClock() { segmentStart = SystemClock.elapsedRealtime() }
    fun pauseClock() {
        if (segmentStart != 0L) {
            accumulated += SystemClock.elapsedRealtime() - segmentStart
            segmentStart = 0L
        }
    }
    fun elapsedMs(): Long = accumulated + if (segmentStart != 0L) SystemClock.elapsedRealtime() - segmentStart else 0L
}
