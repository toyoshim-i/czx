package com.czx.android.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.czx.android.UiSnapshot

enum class BottomLayoutMode(val label: String) {
    KBD_AND_PAD("KBD+PAD"),
    KBD_LARGE("KBD"),
    PAD_LARGE("PAD"),
}

@Composable
fun TouchMenuBar(
    snapshot: UiSnapshot,
    bottomMode: BottomLayoutMode,
    activeKeyboardName: String,
    activePageLabel: String?,
    canTogglePreviewLayout: Boolean,
    setupOpen: Boolean,
    onToggleMenu: () -> Unit,
    onCycleAspect: () -> Unit,
    onCycleBottomMode: () -> Unit,
    onCycleKeyboardPage: () -> Unit,
    onCyclePreviewKeyboard: () -> Unit,
    onToggleSetup: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val aspectLabel = when (snapshot.aspectPolicy) {
        1 -> "4:3"
        2 -> "FULL"
        else -> "PAR"
    }
    val ledActive = snapshot.activityFlags != 0
    val ledColor = when {
        (snapshot.activityFlags and (1 shl 1)) != 0 -> Color(0xFFFF6B57)
        (snapshot.activityFlags and (1 shl 5)) != 0 -> Color(0xFFFFCA4F)
        ledActive -> Color(0xFF56D8FF)
        else -> Color(0xFF3A4454)
    }

    Row(
        modifier = modifier
            .fillMaxWidth()
            .height(42.dp)
            .background(Color(0xFF141820))
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        BarChip(
            label = if (snapshot.menuOpen) "✕ MENU" else "☰ MENU",
            active = snapshot.menuOpen,
            accent = Color(0xFF58D4FF),
            onClick = onToggleMenu,
        )

        Row(
            modifier = Modifier
                .weight(1f)
                .clip(RoundedCornerShape(6.dp))
                .background(Color(0xFF1E2430))
                .clickable(enabled = canTogglePreviewLayout, onClick = onCyclePreviewKeyboard)
                .padding(horizontal = 8.dp, vertical = 5.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Box(
                modifier = Modifier
                    .size(8.dp)
                    .clip(CircleShape)
                    .background(ledColor)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                text = when {
                    snapshot.inLauncher -> "LAUNCHER"
                    snapshot.coreName.isNotBlank() -> snapshot.coreName
                    else -> activeKeyboardName
                },
                color = Color(0xFFE7EEF8),
                fontSize = 11.sp,
                fontWeight = FontWeight.SemiBold,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }

        BarChip(
            label = aspectLabel,
            active = false,
            accent = Color(0xFF7AA7D9),
            onClick = onCycleAspect,
        )

        if (!snapshot.uiActive && activePageLabel != null) {
            BarChip(
                label = activePageLabel,
                active = activePageLabel != "MAIN",
                accent = Color(0xFFB98EFF),
                onClick = onCycleKeyboardPage,
            )
        }

        BarChip(
            label = if (snapshot.uiActive) "UI PAD" else bottomMode.label,
            active = !snapshot.uiActive && bottomMode != BottomLayoutMode.KBD_AND_PAD,
            accent = if (snapshot.uiActive) Color(0xFF4AF2A1) else Color(0xFFF5C842),
            enabled = !snapshot.uiActive,
            onClick = onCycleBottomMode,
        )

        val setupActive = setupOpen || snapshot.coreId == "setup"
        BarChip(
            label = "SETUP",
            active = setupActive || snapshot.hasCoreError,
            accent = if (snapshot.hasCoreError && !setupActive) Color(0xFFFFB74D) else Color(0xFF8BE28B),
            onClick = onToggleSetup,
        )
    }
}

@Composable
private fun BarChip(
    label: String,
    active: Boolean,
    accent: Color,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val bg = if (active) accent.copy(alpha = 0.24f) else Color(0xFF202633)
    val border = if (active) accent else Color(0xFF394457)
    val fg = if (!enabled) accent else if (active) Color.White else Color(0xFFD6E0EE)

    Box(
        modifier = Modifier
            .clip(RoundedCornerShape(6.dp))
            .background(bg)
            .border(1.dp, border, RoundedCornerShape(6.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 9.dp, vertical = 6.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = label,
            color = fg,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            maxLines = 1,
        )
    }
}
