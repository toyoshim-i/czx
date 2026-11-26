package com.czx.android.keyboard

import android.graphics.Paint
import android.graphics.Typeface
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.min

private enum class StickyState {
    OFF,
    ARMED,
    LOCKED,
}

@Composable
fun RetroKeyboardView(
    layout: KeyboardLayout?,
    pageIndex: Int = 0,
    modifier: Modifier = Modifier,
    onSendKey: (KeySpec, Boolean) -> Unit,
) {
    if (layout == null) {
        Box(
            modifier = modifier.background(Color(0xFF1A1D23)),
            contentAlignment = Alignment.Center,
        ) {
            Text("No keyboard layout", color = Color(0xFF9AA4B4), fontSize = 13.sp)
        }
        return
    }

    val page = layout.page(pageIndex)
    val pageKey = "${layout.id}:${page.id}"
    val haptics = LocalHapticFeedback.current
    val activePointers = remember(pageKey) { mutableStateMapOf<PointerId, Int>() }
    val stickyModifiers = remember(pageKey) { mutableStateMapOf<Int, StickyState>() }
    val toggleModifiers = remember(pageKey) { mutableStateMapOf<Int, Boolean>() }
    val stickyUsedWithOtherKey = remember(pageKey) { mutableStateMapOf<Int, Boolean>() }
    val lastTapTimeMs = remember(pageKey) { mutableStateMapOf<Int, Long>() }
    var lastCanvasSize by remember(pageKey) { mutableStateOf(Size.Zero) }

    DisposableEffect(pageKey) {
        onDispose {
            activePointers.values.distinct().forEach { idx ->
                page.keys.getOrNull(idx)?.let { onSendKey(it, false) }
            }
            stickyModifiers.forEach { (idx, state) ->
                if (state != StickyState.OFF) {
                    page.keys.getOrNull(idx)?.let { onSendKey(it, false) }
                }
            }
            toggleModifiers.forEach { (idx, on) ->
                if (on) {
                    page.keys.getOrNull(idx)?.let { onSendKey(it, false) }
                }
            }
        }
    }

    fun computeMetrics(size: Size): Triple<Float, Float, Float> {
        val padX = size.width * 0.015f
        val padY = size.height * 0.025f
        val availW = (size.width - padX * 2f).coerceAtLeast(1f)
        val availH = (size.height - padY * 2f).coerceAtLeast(1f)
        val unitPx = min(availW / page.totalWidthU, availH / page.totalHeightU)
        val originX = (size.width - page.totalWidthU * unitPx) * 0.5f
        val originY = (size.height - page.totalHeightU * unitPx) * 0.5f
        return Triple(originX, originY, unitPx)
    }

    fun keyRect(spec: KeySpec, originX: Float, originY: Float, unitPx: Float): Rect {
        val left = originX + spec.x * unitPx
        val top = originY + spec.y * unitPx
        return Rect(left, top, left + spec.w * unitPx, top + spec.h * unitPx)
    }

    fun hitTest(pos: Offset, size: Size): Int? {
        if (size.width <= 0f || size.height <= 0f) return null
        val (ox, oy, u) = computeMetrics(size)
        val pad = u * 0.08f
        return page.keys.firstOrNull { spec ->
            val r = keyRect(spec, ox, oy, u)
            pos.x in (r.left - pad)..(r.right + pad) &&
                pos.y in (r.top - pad)..(r.bottom + pad)
        }?.index
    }

    fun isKeyHeld(idx: Int): Boolean {
        return activePointers.values.any { it == idx } ||
            (stickyModifiers[idx] ?: StickyState.OFF) != StickyState.OFF ||
            (toggleModifiers[idx] == true)
    }

    fun pressKey(idx: Int) {
        val spec = page.keys.getOrNull(idx) ?: return
        haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
        when (spec.kind) {
            KeyKind.NORMAL -> {
                onSendKey(spec, true)
                stickyModifiers.keys.toList().forEach { modIdx ->
                    if (stickyModifiers[modIdx] == StickyState.ARMED) {
                        stickyUsedWithOtherKey[modIdx] = true
                    }
                }
            }
            KeyKind.MODIFIER_STICKY -> {
                val cur = stickyModifiers[idx] ?: StickyState.OFF
                if (cur == StickyState.OFF) {
                    stickyUsedWithOtherKey[idx] = false
                    onSendKey(spec, true)
                }
            }
            KeyKind.MODIFIER_TOGGLE -> {
                val next = !(toggleModifiers[idx] ?: false)
                toggleModifiers[idx] = next
                onSendKey(spec, next)
            }
        }
    }

    fun releaseKey(idx: Int) {
        val spec = page.keys.getOrNull(idx) ?: return
        when (spec.kind) {
            KeyKind.NORMAL -> {
                onSendKey(spec, false)
                stickyModifiers.keys.toList().forEach { modIdx ->
                    if (stickyModifiers[modIdx] == StickyState.ARMED &&
                        activePointers.values.none { it == modIdx }
                    ) {
                        stickyModifiers[modIdx] = StickyState.OFF
                        stickyUsedWithOtherKey.remove(modIdx)
                        page.keys.getOrNull(modIdx)?.let { onSendKey(it, false) }
                    }
                }
            }
            KeyKind.MODIFIER_STICKY -> {
                val cur = stickyModifiers[idx] ?: StickyState.OFF
                val usedWhileHolding = stickyUsedWithOtherKey[idx] == true
                val now = System.currentTimeMillis()
                val prevTap = lastTapTimeMs[idx] ?: 0L
                if (usedWhileHolding) {
                    stickyModifiers[idx] = StickyState.OFF
                    stickyUsedWithOtherKey.remove(idx)
                    onSendKey(spec, false)
                } else {
                    when (cur) {
                        StickyState.OFF -> {
                            if (now - prevTap < 350L) {
                                stickyModifiers[idx] = StickyState.LOCKED
                            } else {
                                stickyModifiers[idx] = StickyState.ARMED
                            }
                            lastTapTimeMs[idx] = now
                        }
                        StickyState.ARMED -> {
                            if (now - prevTap < 350L) {
                                stickyModifiers[idx] = StickyState.LOCKED
                            } else {
                                stickyModifiers[idx] = StickyState.OFF
                                onSendKey(spec, false)
                            }
                            lastTapTimeMs[idx] = now
                        }
                        StickyState.LOCKED -> {
                            stickyModifiers[idx] = StickyState.OFF
                            onSendKey(spec, false)
                        }
                    }
                }
            }
            KeyKind.MODIFIER_TOGGLE -> Unit
        }
    }

    val mainPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.CENTER
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        }
    }
    val topPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.LEFT
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        }
    }
    val cornerPaint = remember {
        Paint(Paint.ANTI_ALIAS_FLAG).apply {
            textAlign = Paint.Align.RIGHT
            typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.NORMAL)
        }
    }

    Box(
        modifier = modifier
            .background(layout.bezelColor)
            .padding(2.dp)
    ) {
        Canvas(
            modifier = Modifier
                .fillMaxSize()
                .pointerInput(pageKey) {
                    awaitPointerEventScope {
                        while (true) {
                            val event = awaitPointerEvent()
                            event.changes.forEach { change ->
                                val pid = change.id
                                if (!change.pressed) {
                                    val oldIdx = activePointers.remove(pid)
                                    if (oldIdx != null && activePointers.values.none { it == oldIdx }) {
                                        releaseKey(oldIdx)
                                    }
                                    change.consume()
                                } else {
                                    val newIdx = hitTest(change.position, lastCanvasSize)
                                    val oldIdx = activePointers[pid]
                                    if (newIdx != oldIdx) {
                                        if (oldIdx != null) {
                                            activePointers.remove(pid)
                                            if (activePointers.values.none { it == oldIdx }) {
                                                releaseKey(oldIdx)
                                            }
                                        }
                                        if (newIdx != null) {
                                            val alreadyDown = activePointers.values.any { it == newIdx }
                                            activePointers[pid] = newIdx
                                            if (!alreadyDown) {
                                                pressKey(newIdx)
                                            }
                                        }
                                    }
                                    change.consume()
                                }
                            }
                        }
                    }
                }
        ) {
            lastCanvasSize = size
            val (ox, oy, u) = computeMetrics(size)
            val cornerR = CornerRadius(u * 0.12f, u * 0.12f)

            drawRoundRect(
                color = layout.boardColor,
                topLeft = Offset(ox - u * 0.14f, oy - u * 0.12f),
                size = Size(page.totalWidthU * u + u * 0.28f, page.totalHeightU * u + u * 0.24f),
                cornerRadius = CornerRadius(u * 0.2f, u * 0.2f),
            )

            val topLabelSpace = u * 0.18f

            page.keys.forEach { spec ->
                val rect = keyRect(spec, ox, oy, u)
                val held = isKeyHeld(spec.index)
                val sticky = stickyModifiers[spec.index] ?: StickyState.OFF
                val toggled = toggleModifiers[spec.index] == true

                val keyTop = rect.top + if (spec.top.isNotEmpty()) topLabelSpace else 0f
                val keyH = (rect.height - if (spec.top.isNotEmpty()) topLabelSpace else 0f).coerceAtLeast(u * 0.45f)
                val yShift = if (held) u * 0.04f else 0f

                if (spec.top.isNotEmpty()) {
                    topPaint.color = spec.style.topText.toArgb()
                    topPaint.textSize = (u * 0.175f).coerceIn(11f, 24f)
                    drawContext.canvas.nativeCanvas.drawText(
                        spec.top,
                        rect.left + u * 0.04f,
                        rect.top + topLabelSpace - u * 0.03f,
                        topPaint,
                    )
                }

                // Bottom drop shadow
                if (!held) {
                    drawRoundRect(
                        color = Color(0xAA0A0C10),
                        topLeft = Offset(rect.left, keyTop + u * 0.06f),
                        size = Size(rect.width, keyH),
                        cornerRadius = cornerR,
                    )
                }

                val bodyColor = if (held) spec.style.pressedFill else spec.style.fill
                val borderColor = when {
                    sticky == StickyState.LOCKED || toggled -> Color(0xFF4AF2A1)
                    sticky == StickyState.ARMED -> Color(0xFFFFC14D)
                    held -> Color(0xFF7CE0FF)
                    else -> spec.style.border
                }

                drawRoundRect(
                    color = bodyColor,
                    topLeft = Offset(rect.left, keyTop + yShift),
                    size = Size(rect.width, keyH),
                    cornerRadius = cornerR,
                )
                drawRoundRect(
                    color = borderColor,
                    topLeft = Offset(rect.left, keyTop + yShift),
                    size = Size(rect.width, keyH),
                    cornerRadius = cornerR,
                    style = Stroke(width = if (held || sticky != StickyState.OFF || toggled) 2.5f else 1.2f),
                )

                if (sticky != StickyState.OFF || toggled) {
                    drawCircle(
                        color = if (sticky == StickyState.LOCKED || toggled) Color(0xFF4AF2A1) else Color(0xFFFFC14D),
                        radius = u * 0.065f,
                        center = Offset(rect.right - u * 0.14f, keyTop + yShift + u * 0.14f),
                    )
                }

                // Main legend
                val mainScale = when {
                    spec.main.length >= 5 -> 0.21f
                    spec.main.length >= 3 -> 0.25f
                    else -> 0.34f
                }
                mainPaint.color = spec.style.mainText.toArgb()
                mainPaint.textSize = (u * mainScale).coerceIn(13f, 38f)
                val centerX = rect.left + rect.width * (if (spec.shift.isNotEmpty() || spec.sub.isNotEmpty()) 0.42f else 0.50f)
                val baseline = keyTop + yShift + keyH * 0.56f - (mainPaint.ascent() + mainPaint.descent()) * 0.5f
                drawContext.canvas.nativeCanvas.drawText(spec.main, centerX, baseline, mainPaint)

                // Shift legend (upper-right)
                if (spec.shift.isNotEmpty()) {
                    cornerPaint.color = spec.style.shiftText.toArgb()
                    cornerPaint.textSize = (u * 0.21f).coerceIn(11f, 26f)
                    drawContext.canvas.nativeCanvas.drawText(
                        spec.shift,
                        rect.right - u * 0.08f,
                        keyTop + yShift + keyH * 0.34f,
                        cornerPaint,
                    )
                }

                // Sub/Kana legend (lower-right)
                if (spec.sub.isNotEmpty()) {
                    cornerPaint.color = spec.style.subText.toArgb()
                    cornerPaint.textSize = (u * 0.19f).coerceIn(10f, 24f)
                    drawContext.canvas.nativeCanvas.drawText(
                        spec.sub,
                        rect.right - u * 0.08f,
                        keyTop + yShift + keyH * 0.86f,
                        cornerPaint,
                    )
                }
            }
        }
    }
}
