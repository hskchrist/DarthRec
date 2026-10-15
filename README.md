# DarthRec — grabador de pantalla para Android

Graba la pantalla a la resolución y tasa de refresco nativas de tu celular, sin límite de tiempo, sin marca de agua y sin anuncios. Incluye **modo Gameplay** con tu cara en una burbuja flotante.

## Qué hace
- **Se adapta a tu celular**: detecta tu pantalla (resolución y Hz), el codificador de video por hardware y su bitrate máximo, y elige la configuración más alta que tu equipo soporta de verdad.
- **Máxima calidad y fluidez**: hasta la resolución nativa, hasta 144 fps si tu pantalla y tu codificador lo permiten, bitrate alto (VBR), prioridad de tiempo real. H.264 o H.265.
- **Audio**: sonido interno del juego (Android 10+), micrófono, o ambos mezclados (AAC 256 kbps, 48 kHz estéreo).
- **Modo Gameplay**: video horizontal + sonido del juego + tu voz + tu cara.
  - Burbuja de cámara: arrastra para moverla, pellizca para cambiar el tamaño, doble toque para círculo/rectángulo.
- **Pausar / reanudar** sin cortes en el archivo final, desde la app o la notificación.
- **Cuenta regresiva** de 3 s para que alcances a abrir el juego.
- Los videos se guardan en **Películas/DarthRec** y aparecen en la galería.

## Cómo generar el APK

### Opción A — Android Studio (PC)
1. Instala Android Studio.
2. *File → Open* y elige esta carpeta `DarthRec`.
3. Espera a que termine de sincronizar.
4. *Build → Build App Bundle(s) / APK(s) → Build APK(s)*.
5. Pasa el APK (`app/build/outputs/apk/...`) al celular e instálalo (permite "instalar apps desconocidas").

### Opción B — GitHub (sin instalar nada)
1. Sube esta carpeta a un repositorio de GitHub.
2. GitHub compila solo (pestaña **Actions → Compilar APK**).
3. Descarga el archivo **DarthRec-apk** desde el celular, descomprímelo e instala el APK.

## Primer uso
Al tocar **GRABAR**, la app pide solo lo que necesita:
- Micrófono (para cualquier audio — Android lo exige también para el sonido interno).
- Cámara y "Mostrar sobre otras apps" (solo si activas tu cara).
- Notificaciones (para los botones de pausar/detener).
- Permiso de captura de pantalla (Android lo pide en cada grabación; elige **pantalla completa**).

## Límites que pone Android (no la app)
- Algunas apps y juegos bloquean la captura de su audio interno; en esos casos el video sale sin ese sonido.
- Apps con contenido protegido (Netflix, apps de banco) se ven en negro.
- Para grabar a 90/120 fps, la pantalla y el juego deben estar configurados a esa tasa de refresco.
