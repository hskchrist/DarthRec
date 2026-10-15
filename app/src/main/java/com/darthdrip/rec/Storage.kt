package com.darthdrip.rec

import android.content.ContentValues
import android.content.Context
import android.media.MediaScannerConnection
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.ParcelFileDescriptor
import android.provider.MediaStore
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/** Guarda en Películas/DarthRec, visible en la galería. */
object Storage {

    class Output internal constructor(
        val pfd: ParcelFileDescriptor,
        private val uri: Uri?,
        private val file: File?,
    ) {
        fun complete(ctx: Context, ok: Boolean, cb: (Uri?) -> Unit) {
            try { pfd.close() } catch (_: Exception) {}
            val cr = ctx.contentResolver
            if (uri != null && Build.VERSION.SDK_INT >= 29) {
                if (ok) {
                    cr.update(uri, ContentValues().apply { put(MediaStore.Video.Media.IS_PENDING, 0) }, null, null)
                    cb(uri)
                } else {
                    try { cr.delete(uri, null, null) } catch (_: Exception) {}
                    cb(null)
                }
            } else if (file != null) {
                if (ok) {
                    MediaScannerConnection.scanFile(ctx, arrayOf(file.absolutePath), arrayOf("video/mp4")) { _, u -> cb(u) }
                } else {
                    file.delete()
                    cb(null)
                }
            } else {
                cb(null)
            }
        }
    }

    fun create(ctx: Context): Output {
        val name = "DarthRec_" + SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date()) + ".mp4"
        return if (Build.VERSION.SDK_INT >= 29) {
            val values = ContentValues().apply {
                put(MediaStore.Video.Media.DISPLAY_NAME, name)
                put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                put(MediaStore.Video.Media.RELATIVE_PATH, Environment.DIRECTORY_MOVIES + "/DarthRec")
                put(MediaStore.Video.Media.IS_PENDING, 1)
            }
            val collection = MediaStore.Video.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            val uri = ctx.contentResolver.insert(collection, values)
                ?: throw IllegalStateException("No se pudo crear el archivo de video")
            val pfd = ctx.contentResolver.openFileDescriptor(uri, "rw")
                ?: throw IllegalStateException("No se pudo abrir el archivo de video")
            Output(pfd, uri, null)
        } else {
            @Suppress("DEPRECATION")
            val dir = File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MOVIES), "DarthRec")
            dir.mkdirs()
            val file = File(dir, name)
            val pfd = ParcelFileDescriptor.open(
                file,
                ParcelFileDescriptor.MODE_READ_WRITE or ParcelFileDescriptor.MODE_CREATE or ParcelFileDescriptor.MODE_TRUNCATE,
            )
            Output(pfd, null, file)
        }
    }
}
