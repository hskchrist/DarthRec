@file:OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)

package com.darthdrip.rec

import android.net.Uri
import android.os.Build
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.darthdrip.rec.RecorderState.Status
import kotlinx.coroutines.delay
import java.util.Locale

private val Red = Color(0xFFFF3B3B)

@Composable
fun DarthTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Red,
            onPrimary = Color.White,
            secondary = Color(0xFFFF8A80),
            secondaryContainer = Color(0xFF4A1C20),
            onSecondaryContainer = Color.White,
            background = Color(0xFF0D0D10),
            surface = Color(0xFF16161B),
            surfaceVariant = Color(0xFF1E1E25),
            surfaceContainerHigh = Color(0xFF1E1E25),
        ),
        content = content,
    )
}

@Composable
fun RecorderScreen(
    info: DeviceInfo,
    initial: RecSettings,
    isLandscapeNow: Boolean,
    onSettingsChange: (RecSettings) -> Unit,
    onRecord: (RecSettings, Plan) -> Unit,
    onPause: () -> Unit,
    onResume: () -> Unit,
    onStop: () -> Unit,
    onOpen: (Uri) -> Unit,
) {
    var s by remember { mutableStateOf(initial) }
    fun update(n: RecSettings) { s = n; onSettingsChange(n) }

    val status by RecorderState.status.collectAsState()
    val message by RecorderState.message.collectAsState()
    val lastUri by RecorderState.lastUri.collectAsState()
    var elapsed by remember { mutableLongStateOf(0L) }
    LaunchedEffect(status) {
        while (status == Status.RECORDING || status == Status.PAUSED) {
            elapsed = RecorderState.elapsedMs()
            delay(250)
        }
    }

    val enc = info.encoders.firstOrNull { it.mime == s.mime } ?: info.encoders.firstOrNull()
    val fpsOptions = listOf(30, 60, 90, 120, 144).filter { it <= info.maxHz + 2 }.ifEmpty { listOf(30) }
    val fps = if (s.fps == 0 || s.fps !in fpsOptions) fpsOptions.last() else s.fps
    val landscape = when (s.orientation) { 1 -> false; 2 -> true; else -> isLandscapeNow }
    val plan = remember(s, enc, landscape, fps) {
        enc?.let { DeviceCaps.plan(info, it, s.scale, fps, s.quality, landscape) }
    }
    val idle = status == Status.IDLE
    val api29 = Build.VERSION.SDK_INT >= 29

    Scaffold(containerColor = MaterialTheme.colorScheme.background) { pad ->
        Column(
            Modifier
                .fillMaxSize()
                .padding(pad)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Column {
                Text("DarthRec", fontSize = 30.sp, fontWeight = FontWeight.Black)
                Text(
                    "Grabador de pantalla · sin límite de tiempo · sin marca de agua",
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f),
                    fontSize = 13.sp,
                )
            }

            RecordControls(
                status = status,
                elapsed = elapsed,
                enabled = plan != null,
                onRecord = { plan?.let { onRecord(s.copy(fps = fps), it) } },
                onPause = onPause,
                onResume = onResume,
                onStop = onStop,
            )

            message?.let { msg ->
                Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
                    Row(Modifier.padding(start = 16.dp, end = 4.dp, top = 4.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(msg, Modifier.weight(1f), fontSize = 14.sp)
                        TextButton(onClick = { RecorderState.message.value = null }) { Text("OK") }
                    }
                }
            }
            lastUri?.let { uri ->
                if (idle) OutlinedButton(onClick = { onOpen(uri) }, Modifier.fillMaxWidth()) { Text("▶  Ver último video") }
            }

            Summary(plan, fps)

            Section("Modo") {
                Chips(listOf(false to "Normal", true to "🎮 Gameplay + mi cara"), s.gameplay, idle) { on ->
                    update(
                        if (on) s.copy(gameplay = true, orientation = 2, internalAudio = true, mic = true, facecam = true)
                        else s.copy(gameplay = false, facecam = false, orientation = 0)
                    )
                }
                if (s.gameplay) Hint("Graba el juego en horizontal con el sonido del juego, tu voz y tu cara en una burbuja flotante.")
            }

            Section("Video") {
                Label("Resolución")
                Chips(listOf(1f to "Nativa", 0.75f to "75 %", 0.5f to "50 %"), s.scale, idle) { update(s.copy(scale = it)) }
                Label("Fotogramas por segundo")
                Chips(fpsOptions.map { it to "$it fps" }, fps, idle) { update(s.copy(fps = it)) }
                Label("Calidad")
                Chips(Quality.entries.map { it to it.label }, s.quality, idle) { update(s.copy(quality = it)) }
                if (info.encoders.size > 1) {
                    Label("Códec")
                    Chips(info.encoders.map { it.mime to it.label }, enc?.mime, idle) { update(s.copy(mime = it)) }
                    Hint("H.264 es compatible con todo. H.265 da más calidad por MB.")
                }
                Label("Orientación del video")
                Chips(listOf(0 to "Auto", 1 to "Vertical", 2 to "Horizontal"), s.orientation, idle) { update(s.copy(orientation = it)) }
            }

            Section("Audio") {
                SwitchRow(
                    "Sonido interno (juego / apps)",
                    if (api29) "Sin ruido del ambiente" else "Requiere Android 10 o superior",
                    s.internalAudio && api29,
                    idle && api29,
                ) { update(s.copy(internalAudio = it)) }
                SwitchRow("Micrófono (tu voz)", null, s.mic, idle) { update(s.copy(mic = it)) }
            }

            Section("Cámara (facecam)") {
                SwitchRow(
                    "Grabar mi cara",
                    "Burbuja flotante: arrástrala, pellizca para cambiar el tamaño, doble toque cambia la forma",
                    s.facecam, idle,
                ) { update(s.copy(facecam = it)) }
                if (s.facecam) {
                    Label("Tamaño inicial: ${s.camSize} dp")
                    Slider(
                        value = s.camSize.toFloat(),
                        onValueChange = { update(s.copy(camSize = it.toInt())) },
                        valueRange = 90f..300f,
                        enabled = idle,
                    )
                    Chips(listOf(true to "Círculo", false to "Rectángulo"), s.camCircle, idle) { update(s.copy(camCircle = it)) }
                }
            }

            Section("Otros") {
                SwitchRow("Cuenta regresiva de 3 s", "Te da tiempo de abrir el juego", s.countdown, idle) {
                    update(s.copy(countdown = it))
                }
            }

            DeviceCard(info)
            Spacer(Modifier.height(24.dp))
        }
    }
}

@Composable
private fun RecordControls(
    status: Status,
    elapsed: Long,
    enabled: Boolean,
    onRecord: () -> Unit,
    onPause: () -> Unit,
    onResume: () -> Unit,
    onStop: () -> Unit,
) {
    when (status) {
        Status.IDLE -> Button(
            onClick = onRecord,
            enabled = enabled,
            modifier = Modifier.fillMaxWidth().height(64.dp),
            shape = RoundedCornerShape(20.dp),
            colors = ButtonDefaults.buttonColors(containerColor = Red),
        ) { Text("●  GRABAR", fontSize = 20.sp, fontWeight = FontWeight.Bold) }

        Status.COUNTDOWN -> Card(Modifier.fillMaxWidth()) {
            Row(Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 3.dp)
                Spacer(Modifier.width(12.dp))
                Text("Empieza en 3 segundos… abre tu juego", fontSize = 16.sp)
            }
        }

        Status.RECORDING, Status.PAUSED -> Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(if (status == Status.PAUSED) "⏸" else "●", color = Red, fontSize = 22.sp)
                    Spacer(Modifier.width(10.dp))
                    Text(formatTime(elapsed), fontSize = 34.sp, fontWeight = FontWeight.Bold, fontFamily = FontFamily.Monospace)
                }
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    OutlinedButton(
                        onClick = if (status == Status.PAUSED) onResume else onPause,
                        modifier = Modifier.weight(1f).height(52.dp),
                    ) { Text(if (status == Status.PAUSED) "Reanudar" else "Pausar") }
                    Button(
                        onClick = onStop,
                        modifier = Modifier.weight(1f).height(52.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = Red),
                    ) { Text("■  Detener") }
                }
            }
        }

        Status.SAVING -> Card(Modifier.fillMaxWidth()) {
            Row(Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically) {
                CircularProgressIndicator(Modifier.size(24.dp), strokeWidth = 3.dp)
                Spacer(Modifier.width(12.dp))
                Text("Guardando video…", fontSize = 16.sp)
            }
        }
    }
}

@Composable
private fun Summary(plan: Plan?, wantedFps: Int) {
    Card(
        Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(Modifier.padding(16.dp)) {
            if (plan == null) {
                Text("Tu celular no tiene un codificador de video disponible.", color = Red)
                return@Column
            }
            Text("Se grabará en", fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.6f))
            Text(
                "${plan.width} × ${plan.height} · ${plan.fps} fps",
                fontSize = 22.sp, fontWeight = FontWeight.Bold,
            )
            val mbps = plan.bitrate / 1_000_000.0
            val mbPerMin = (plan.bitrate + 256_000) * 60.0 / 8 / 1_000_000
            Text(
                String.format(Locale.US, "%.0f Mbps · %s · ≈ %.0f MB por minuto", mbps, plan.encoder.label, mbPerMin),
                fontSize = 14.sp,
            )
            if (plan.fps < wantedFps) {
                Hint("Ajustado: el codificador de tu celular no soporta $wantedFps fps a esa resolución.")
            }
        }
    }
}

@Composable
private fun DeviceCard(info: DeviceInfo) {
    Section("Tu dispositivo") {
        Text(info.model, fontWeight = FontWeight.SemiBold)
        Text(info.android, fontSize = 13.sp)
        Text("Pantalla ${info.nativeW} × ${info.nativeH} · hasta ${info.maxHz} Hz", fontSize = 13.sp)
        if (info.encoders.isEmpty()) Text("Sin codificador de video", color = Red)
        info.encoders.forEach {
            Text(
                "${it.label}: ${if (it.hardware) "hardware" else "software"} · hasta ${it.maxBitrate / 1_000_000} Mbps",
                fontSize = 13.sp,
            )
        }
        Hint("Para grabar a ${info.maxHz} fps, activa la tasa de refresco alta en Ajustes > Pantalla y en los gráficos del juego.")
    }
}

@Composable
private fun Section(title: String, content: @Composable () -> Unit) {
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(title, fontWeight = FontWeight.Bold, fontSize = 16.sp)
            content()
        }
    }
}

@Composable
private fun Label(text: String) {
    Text(text, fontSize = 13.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f))
}

@Composable
private fun Hint(text: String) {
    Text(text, fontSize = 12.sp, color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.55f))
}

@Composable
private fun <T> Chips(options: List<Pair<T, String>>, selected: T?, enabled: Boolean, onSelect: (T) -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        options.forEach { (value, label) ->
            FilterChip(
                selected = value == selected,
                onClick = { onSelect(value) },
                label = { Text(label) },
                enabled = enabled,
            )
        }
    }
}

@Composable
private fun SwitchRow(title: String, subtitle: String?, checked: Boolean, enabled: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title)
            if (subtitle != null) Hint(subtitle)
        }
        Spacer(Modifier.width(8.dp))
        Switch(checked = checked, onCheckedChange = onChange, enabled = enabled)
    }
}

private fun formatTime(ms: Long): String {
    val t = ms / 1000
    val h = t / 3600
    val m = (t % 3600) / 60
    val sec = t % 60
    return if (h > 0) String.format(Locale.US, "%d:%02d:%02d", h, m, sec)
    else String.format(Locale.US, "%02d:%02d", m, sec)
}
