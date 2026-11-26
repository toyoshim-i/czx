package com.czx.android.ui

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import com.czx.android.CzxPad
import kotlin.math.hypot
import kotlin.math.min

private data class PadButtonCircle(
    val mask: Int,
    val center: Offset,
    val radius: Float,
    val label: String,
    val subLabel: String,
    val color: Color,
)

private data class PadButtonPill(
    val mask: Int,
    val topLeft: Offset,
    val size: Size,
    val label: String,
)

@Composable
fun VirtualGamepad(
    compact: Boolean,
    uiGuideMode: Boolean,
    onButtonsChanged: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val haptics = LocalHapticFeedback.current
    val pointerMasks = remember { mutableStateMapOf<PointerId, Int>() }
    var lastEmittedMask by remember { mutableStateOf(0) }
    var canvasSize by remember { mutableStateOf(Size.Zero) }

    DisposableEffect(Unit) {
        onDispose {
            if (lastEmittedMask != 0) {
                onButtonsChanged(0)
            }
        }
    }

    fun emitMaskIfChanged() {
        var combined = 0
        pointerMasks.values.forEach { combined = combined or it }
        if (combined != lastEmittedMask) {
            if ((combined and lastEmittedMask.inv()) != 0) {
                haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
            }
            lastEmittedMask = combined
            onButtonsChanged(combined)
        }
    }

    fun computeLayout(size: Size): Triple<Pair<Offset, Float>, List<PadButtonCircle>, List<PadButtonPill>> {
        val w = size.width.coerceAtLeast(1f)
        val h = size.height.coerceAtLeast(1f)
        val dpadRadius = min(w * 0.21f, h * (if (compact) 0.40f else 0.28f)).coerceAtLeast(36f)
        val dpadCenter = Offset(w * 0.23f, h * (if (compact) 0.50f else 0.53f))

        val btnR = (dpadRadius * 0.30f).coerceAtLeast(18f)
        val abCenter = Offset(w * 0.77f, h * (if (compact) 0.50f else 0.53f))
        val spread = btnR * 2.15f

        val circles = buildList {
            add(
                PadButtonCircle(
                    mask = CzxPad.SOUTH,
                    center = Offset(abCenter.x, abCenter.y + spread * 0.65f),
                    radius = btnR,
                    label = "A",
                    subLabel = if (uiGuideMode) "決定" else "",
                    color = Color(0xFF2FA86E),
                )
            )
            add(
                PadButtonCircle(
                    mask = CzxPad.EAST,
                    center = Offset(abCenter.x + spread * 0.88f, abCenter.y),
                    radius = btnR,
                    label = "B",
                    subLabel = if (uiGuideMode) "戻る" else "",
                    color = Color(0xFFC94A4A),
                )
            )
            add(
                PadButtonCircle(
                    mask = CzxPad.WEST,
                    center = Offset(abCenter.x - spread * 0.88f, abCenter.y),
                    radius = btnR,
                    label = "X",
                    subLabel = if (uiGuideMode) "上/抜" else "",
                    color = Color(0xFF3C78C8),
                )
            )
            add(
                PadButtonCircle(
                    mask = CzxPad.NORTH,
                    center = Offset(abCenter.x, abCenter.y - spread * 0.65f),
                    radius = btnR,
                    label = "Y",
                    subLabel = "",
                    color = Color(0xFFD1932C),
                )
            )
        }

        val pillW = (w * 0.14f).coerceIn(72f, 150f)
        val pillH = (h * (if (compact) 0.16f else 0.095f)).coerceIn(28f, 56f)
        val pills = buildList {
            if (!compact) {
                add(
                    PadButtonPill(
                        mask = CzxPad.L1,
                        topLeft = Offset(w * 0.06f, h * 0.06f),
                        size = Size(pillW, pillH),
                        label = if (uiGuideMode) "L1 PgUp" else "L1",
                    )
                )
                add(
                    PadButtonPill(
                        mask = CzxPad.R1,
                        topLeft = Offset(w * 0.94f - pillW, h * 0.06f),
                        size = Size(pillW, pillH),
                        label = if (uiGuideMode) "R1 PgDn" else "R1",
                    )
                )
            }
            val bottomY = h - pillH - h * 0.06f
            add(
                PadButtonPill(
                    mask = CzxPad.SELECT,
                    topLeft = Offset(w * 0.50f - pillW - 10f, bottomY),
                    size = Size(pillW, pillH),
                    label = "SELECT",
                )
            )
            add(
                PadButtonPill(
                    mask = CzxPad.START,
                    topLeft = Offset(w * 0.50f + 10f, bottomY),
                    size = Size(pillW, pillH),
                    label = if (uiGuideMode) "START" else "START",
                )
            )
        }

        return Triple(Pair(dpadCenter, dpadRadius), circles, pills)
    }

    fun maskForPoint(pos: Offset, size: Size): Int {
        if (size.width <= 0f || size.height <= 0f) return 0
        val (dpad, circles, pills) = computeLayout(size)
        val (dpadCenter, dpadR) = dpad
        var mask = 0

        val dx = pos.x - dpadCenter.x
        val dy = pos.y - dpadCenter.y
        val dist = hypot(dx, dy)
        if (dist <= dpadR * 1.35f && dist >= dpadR * 0.18f) {
            val dead = dpadR * 0.25f
            if (dx < -dead) mask = mask or CzxPad.LEFT
            if (dx > dead) mask = mask or CzxPad.RIGHT
            if (dy < -dead) mask = mask or CzxPad.UP
            if (dy > dead) mask = mask or CzxPad.DOWN
        }

        circles.forEach { c ->
            if (hypot(pos.x - c.center.x, pos.y - c.center.y) <= c.radius * 1.25f) {
                mask = mask or c.mask
            }
        }

        pills.forEach { p ->
            val pad = 10f
            if (pos.x in (p.topLeft.x - pad)..(p.topLeft.x + p.size.width + pad) &&
                pos.y in (p.topLeft.y - pad)..(p.topLeft.y + p.size.height + pad)
            ) {
                mask = mask or p.mask
            }
        }
        return mask
    }

    val labelPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        }
    }
    val guidePaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        }
    }

    Canvas(
        modifier = modifier
            .fillMaxSize()
            .background(if (uiGuideMode) Color(0xFF151A22) else Color(0xFF181C24))
            .pointerInput(compact, uiGuideMode) {
                awaitPointerEventScope {
                    while (true) {
                        val event = awaitPointerEvent()
                        event.changes.forEach { change ->
                            if (!change.pressed) {
                                pointerMasks.remove(change.id)
                            } else {
                                pointerMasks[change.id] = maskForPoint(change.position, canvasSize)
                            }
                            change.consume()
                        }
                        emitMaskIfChanged()
                    }
                }
            }
    ) {
        canvasSize = size
        val (dpad, circles, pills) = computeLayout(size)
        val (dpadCenter, dpadR) = dpad
        val curMask = lastEmittedMask

        // D-Pad outer dish
        drawCircle(
            color = Color(0xFF232934),
            radius = dpadR * 1.05f,
            center = dpadCenter,
        )
        drawCircle(
            color = Color(0xFF3A4456),
            radius = dpadR * 1.05f,
            center = dpadCenter,
            style = Stroke(width = 2f),
        )

        val armW = dpadR * 0.56f
        val armLen = dpadR * 0.90f
        // Cross base
        drawRoundRect(
            color = Color(0xFF333B4A),
            topLeft = Offset(dpadCenter.x - armLen, dpadCenter.y - armW * 0.5f),
            size = Size(armLen * 2f, armW),
            cornerRadius = CornerRadius(12f, 12f),
        )
        drawRoundRect(
            color = Color(0xFF333B4A),
            topLeft = Offset(dpadCenter.x - armW * 0.5f, dpadCenter.y - armLen),
            size = Size(armW, armLen * 2f),
            cornerRadius = CornerRadius(12f, 12f),
        )

        // Highlight active directions
        val dirColor = Color(0xFF5CE1FF)
        if ((curMask and CzxPad.UP) != 0) {
            drawRoundRect(
                color = dirColor.copy(alpha = 0.55f),
                topLeft = Offset(dpadCenter.x - armW * 0.45f, dpadCenter.y - armLen),
                size = Size(armW * 0.9f, armLen * 0.68f),
                cornerRadius = CornerRadius(10f, 10f),
            )
        }
        if ((curMask and CzxPad.DOWN) != 0) {
            drawRoundRect(
                color = dirColor.copy(alpha = 0.55f),
                topLeft = Offset(dpadCenter.x - armW * 0.45f, dpadCenter.y + armLen * 0.32f),
                size = Size(armW * 0.9f, armLen * 0.68f),
                cornerRadius = CornerRadius(10f, 10f),
            )
        }
        if ((curMask and CzxPad.LEFT) != 0) {
            drawRoundRect(
                color = dirColor.copy(alpha = 0.55f),
                topLeft = Offset(dpadCenter.x - armLen, dpadCenter.y - armW * 0.45f),
                size = Size(armLen * 0.68f, armW * 0.9f),
                cornerRadius = CornerRadius(10f, 10f),
            )
        }
        if ((curMask and CzxPad.RIGHT) != 0) {
            drawRoundRect(
                color = dirColor.copy(alpha = 0.55f),
                topLeft = Offset(dpadCenter.x + armLen * 0.32f, dpadCenter.y - armW * 0.45f),
                size = Size(armLen * 0.68f, armW * 0.9f),
                cornerRadius = CornerRadius(10f, 10f),
            )
        }

        // Action buttons
        circles.forEach { c ->
            val pressed = (curMask and c.mask) != 0
            drawCircle(
                color = if (pressed) c.color else c.color.copy(alpha = 0.36f),
                radius = c.radius,
                center = c.center,
            )
            drawCircle(
                color = if (pressed) Color.White else c.color,
                radius = c.radius,
                center = c.center,
                style = Stroke(width = if (pressed) 3.5f else 2f),
            )
            labelPaint.color = Color.White.toArgb()
            labelPaint.textSize = (c.radius * 0.72f).coerceIn(18f, 44f)
            val yOff = if (c.subLabel.isNotEmpty()) -c.radius * 0.14f else 0f
            drawContext.canvas.nativeCanvas.drawText(
                c.label,
                c.center.x,
                c.center.y + yOff - (labelPaint.ascent() + labelPaint.descent()) * 0.5f,
                labelPaint,
            )
            if (c.subLabel.isNotEmpty()) {
                guidePaint.color = Color(0xFFE6F0FF).toArgb()
                guidePaint.textSize = (c.radius * 0.36f).coerceIn(12f, 24f)
                drawContext.canvas.nativeCanvas.drawText(
                    c.subLabel,
                    c.center.x,
                    c.center.y + c.radius * 0.48f,
                    guidePaint,
                )
            }
        }

        // Pill buttons (SELECT / START / L1 / R1)
        pills.forEach { p ->
            val pressed = (curMask and p.mask) != 0
            drawRoundRect(
                color = if (pressed) Color(0xFF4A78B8) else Color(0xFF28303E),
                topLeft = p.topLeft,
                size = p.size,
                cornerRadius = CornerRadius(p.size.height * 0.5f, p.size.height * 0.5f),
            )
            drawRoundRect(
                color = if (pressed) Color(0xFF8BE2FF) else Color(0xFF4B576C),
                topLeft = p.topLeft,
                size = p.size,
                cornerRadius = CornerRadius(p.size.height * 0.5f, p.size.height * 0.5f),
                style = Stroke(width = 1.6f),
            )
            labelPaint.color = Color(0xFFE4ECF8).toArgb()
            labelPaint.textSize = (p.size.height * 0.42f).coerceIn(14f, 28f)
            drawContext.canvas.nativeCanvas.drawText(
                p.label,
                p.topLeft.x + p.size.width * 0.5f,
                p.topLeft.y + p.size.height * 0.5f - (labelPaint.ascent() + labelPaint.descent()) * 0.5f,
                labelPaint,
            )
        }
    }
}
