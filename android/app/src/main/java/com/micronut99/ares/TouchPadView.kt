package com.micronut99.ares

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.Build
import android.os.VibrationEffect
import android.os.Vibrator
import android.view.MotionEvent
import android.view.View
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.min

/** An on-screen 6-button pad: D-pad, A B C / X Y Z, Start, Mode. Multi-touch, slide between buttons. */
class TouchPadView(context: Context, private val onChange: (Int) -> Unit) : View(context) {
    private class Btn(val bit: Int, val label: String, var cx: Float = 0f, var cy: Float = 0f, var r: Float = 0f)

    private val buttons = listOf(
        Btn(NativeBridge.A, "A"), Btn(NativeBridge.B, "B"), Btn(NativeBridge.C, "C"),
        Btn(NativeBridge.X, "X"), Btn(NativeBridge.Y, "Y"), Btn(NativeBridge.Z, "Z"),
        Btn(NativeBridge.START, "START"), Btn(NativeBridge.MODE, "MODE"),
    )
    private var padX = 0f; private var padY = 0f; private var padR = 0f
    private var mask = 0
    var opacity = 0.45f
        set(v) { field = v; invalidate() }
    var vibrate = true

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 3f }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply { textAlign = Paint.Align.CENTER; isFakeBoldText = true }
    private val vibrator = context.getSystemService(Vibrator::class.java)

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val unit = min(w, h) / 9.5f            //button radius scale
        val bottom = h - unit * 0.6f
        padR = unit * 2.1f
        padX = unit * 0.6f + padR
        padY = bottom - padR
        //face buttons: A B C on the lower row, X Y Z above, slanted like the 6-button pad
        val r = unit * 0.82f
        val gap = r * 2.25f
        val right = w - unit * 0.6f - r
        val lowY = bottom - r
        val highY = lowY - gap * 0.95f
        listOf("C" to 0, "B" to 1, "A" to 2).forEach { (l, i) -> place(l, right - gap * i, lowY - i * r * 0.35f, r) }
        listOf("Z" to 0, "Y" to 1, "X" to 2).forEach { (l, i) -> place(l, right - gap * i, highY - i * r * 0.35f, r * 0.85f) }
        place("START", w / 2f + unit * 1.3f, h - unit * 0.75f, unit * 0.62f)
        place("MODE", w / 2f - unit * 1.3f, h - unit * 0.75f, unit * 0.62f)
        text.textSize = unit * 0.55f
    }

    private fun place(label: String, x: Float, y: Float, r: Float) {
        buttons.first { it.label == label }.apply { cx = x; cy = y; this.r = r }
    }

    override fun onDraw(canvas: Canvas) {
        val a = (opacity * 255).toInt().coerceIn(0, 255)
        //D-pad
        fill.color = Color.argb(a / 2, 40, 44, 52); canvas.drawCircle(padX, padY, padR, fill)
        stroke.color = Color.argb(a, 230, 230, 230); canvas.drawCircle(padX, padY, padR, stroke)
        val arm = padR * 0.38f
        for ((bit, rect) in listOf(
            NativeBridge.UP to RectF(padX - arm, padY - padR * 0.92f, padX + arm, padY - arm),
            NativeBridge.DOWN to RectF(padX - arm, padY + arm, padX + arm, padY + padR * 0.92f),
            NativeBridge.LEFT to RectF(padX - padR * 0.92f, padY - arm, padX - arm, padY + arm),
            NativeBridge.RIGHT to RectF(padX + arm, padY - arm, padX + padR * 0.92f, padY + arm),
        )) {
            fill.color = if ((mask and bit) != 0) Color.argb(a, 242, 163, 58) else Color.argb(a, 200, 200, 205)
            canvas.drawRoundRect(rect, 8f, 8f, fill)
        }
        for (b in buttons) {
            val down = (mask and b.bit) != 0
            fill.color = if (down) Color.argb(a, 242, 163, 58) else Color.argb(a / 2, 40, 44, 52)
            canvas.drawCircle(b.cx, b.cy, b.r, fill)
            canvas.drawCircle(b.cx, b.cy, b.r, stroke)
            text.color = Color.argb(a, 255, 255, 255)
            text.textSize = if (b.label.length > 1) b.r * 0.55f else b.r * 0.9f
            canvas.drawText(b.label, b.cx, b.cy + text.textSize * 0.35f, text)
        }
    }

    override fun onTouchEvent(e: MotionEvent): Boolean {
        var next = 0
        for (i in 0 until e.pointerCount) {
            val action = e.actionMasked
            if ((action == MotionEvent.ACTION_POINTER_UP && i == e.actionIndex) || action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) continue
            next = next or hit(e.getX(i), e.getY(i))
        }
        if (next != mask) {
            val pressed = next and mask.inv()
            mask = next
            onChange(mask)
            if (pressed != 0 && vibrate && vibrator != null && Build.VERSION.SDK_INT >= 26) {
                vibrator.vibrate(VibrationEffect.createOneShot(12, 80))
            }
            invalidate()
        }
        return true
    }

    private fun hit(x: Float, y: Float): Int {
        val d = hypot(x - padX, y - padY)
        if (d < padR * 1.25f && d > padR * 0.18f) {
            //8 directions, 45° each
            val angle = Math.toDegrees(atan2((padY - y).toDouble(), (x - padX).toDouble())).let { if (it < 0) it + 360 else it }
            var bits = 0
            if (angle < 67.5 || angle >= 292.5) bits = bits or NativeBridge.RIGHT
            if (angle in 22.5..157.5) bits = bits or NativeBridge.UP
            if (angle in 112.5..247.5) bits = bits or NativeBridge.LEFT
            if (angle in 202.5..337.5) bits = bits or NativeBridge.DOWN
            return bits
        }
        var best: Btn? = null; var bestD = Float.MAX_VALUE
        for (b in buttons) {
            val bd = hypot(x - b.cx, y - b.cy)
            if (bd < b.r * 1.3f && bd < bestD) { best = b; bestD = bd }
        }
        return best?.bit ?: 0
    }
}
