package com.darthdrip.rec

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Color
import android.graphics.Outline
import android.graphics.PixelFormat
import android.graphics.drawable.GradientDrawable
import android.util.Log
import android.view.GestureDetector
import android.view.Gravity
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.ViewGroup
import android.view.ViewOutlineProvider
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.camera.core.CameraSelector
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner

/**
 * Burbuja flotante con la cámara frontal. Como está sobre la pantalla, queda dentro del video.
 * Arrastrar = mover · Pellizcar = tamaño · Doble toque = círculo / rectángulo.
 */
class FacecamOverlay(
    private val ctx: Context,
    private val owner: LifecycleOwner,
    sizeDp: Int,
    private var circle: Boolean,
) {
    private val wm = ctx.getSystemService(WindowManager::class.java)
    private val density = ctx.resources.displayMetrics.density
    private var sizePx = (sizeDp * density).toInt()
    private var root: FrameLayout? = null
    private var provider: ProcessCameraProvider? = null

    fun show() {
        val preview = PreviewView(ctx).apply {
            implementationMode = PreviewView.ImplementationMode.COMPATIBLE // TextureView: permite recortar en círculo
            scaleType = PreviewView.ScaleType.FILL_CENTER
        }
        val frame = FrameLayout(ctx)
        frame.addView(preview, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        frame.clipToOutline = true
        applyShape(frame)

        val lp = WindowManager.LayoutParams(
            sizePx, sizePx,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
            PixelFormat.TRANSLUCENT,
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = (16 * density).toInt()
            y = (96 * density).toInt()
        }
        attachTouch(frame, lp)
        wm.addView(frame, lp)
        root = frame

        val future = ProcessCameraProvider.getInstance(ctx)
        future.addListener({
            try {
                val p = future.get()
                provider = p
                val selector = if (p.hasCamera(CameraSelector.DEFAULT_FRONT_CAMERA))
                    CameraSelector.DEFAULT_FRONT_CAMERA else CameraSelector.DEFAULT_BACK_CAMERA
                val pv = Preview.Builder().build().also { it.setSurfaceProvider(preview.surfaceProvider) }
                p.unbindAll()
                p.bindToLifecycle(owner, selector, pv)
            } catch (e: Exception) {
                Log.e("DarthRec", "camera", e)
                RecorderState.message.value = "No se pudo abrir la cámara: ${e.message}"
            }
        }, ContextCompat.getMainExecutor(ctx))
    }

    fun hide() {
        try { provider?.unbindAll() } catch (_: Exception) {}
        root?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        root = null
        provider = null
    }

    private fun applyShape(v: View) {
        val r = 20 * density
        v.outlineProvider = object : ViewOutlineProvider() {
            override fun getOutline(view: View, o: Outline) {
                if (circle) o.setOval(0, 0, view.width, view.height)
                else o.setRoundRect(0, 0, view.width, view.height, r)
            }
        }
        v.invalidateOutline()
        v.foreground = GradientDrawable().apply {
            shape = if (circle) GradientDrawable.OVAL else GradientDrawable.RECTANGLE
            if (!circle) cornerRadius = r
            setColor(Color.TRANSPARENT)
            setStroke((3 * density).toInt(), Color.WHITE)
        }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun attachTouch(v: View, lp: WindowManager.LayoutParams) {
        val minPx = (72 * density).toInt()
        val maxPx = (420 * density).toInt()
        val scaler = ScaleGestureDetector(ctx, object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(d: ScaleGestureDetector): Boolean {
                sizePx = (sizePx * d.scaleFactor).toInt().coerceIn(minPx, maxPx)
                lp.width = sizePx
                lp.height = sizePx
                wm.updateViewLayout(v, lp)
                return true
            }
        })
        val taps = GestureDetector(ctx, object : GestureDetector.SimpleOnGestureListener() {
            override fun onDoubleTap(e: MotionEvent): Boolean {
                circle = !circle
                applyShape(v)
                return true
            }
        })
        var sx = 0f
        var sy = 0f
        var px = 0
        var py = 0
        var reanchor = false
        v.setOnTouchListener { _, e ->
            scaler.onTouchEvent(e)
            taps.onTouchEvent(e)
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    sx = e.rawX; sy = e.rawY; px = lp.x; py = lp.y
                }
                MotionEvent.ACTION_POINTER_UP -> reanchor = true
                MotionEvent.ACTION_MOVE -> if (!scaler.isInProgress && e.pointerCount == 1) {
                    if (reanchor) {
                        sx = e.rawX; sy = e.rawY; px = lp.x; py = lp.y
                        reanchor = false
                    }
                    lp.x = px + (e.rawX - sx).toInt()
                    lp.y = py + (e.rawY - sy).toInt()
                    wm.updateViewLayout(v, lp)
                }
            }
            true
        }
    }
}
