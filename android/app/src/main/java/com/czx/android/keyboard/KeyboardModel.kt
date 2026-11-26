package com.czx.android.keyboard

import android.content.Context
import androidx.compose.ui.graphics.Color
import org.json.JSONArray
import org.json.JSONObject

enum class KeyKind {
    NORMAL,
    MODIFIER_STICKY,
    MODIFIER_TOGGLE,
}

enum class KeyStyle(
    val fill: Color,
    val pressedFill: Color,
    val border: Color,
    val mainText: Color,
    val topText: Color,
    val shiftText: Color,
    val subText: Color,
) {
    ALPHA(
        fill = Color(0xFF3E4552),
        pressedFill = Color(0xFF5A6475),
        border = Color(0xFF667082),
        mainText = Color(0xFFF4F6FA),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFF7AD7FF),
        subText = Color(0xFFB6C2D4),
    ),
    FUNC(
        fill = Color(0xFF2D333E),
        pressedFill = Color(0xFF475060),
        border = Color(0xFF555E70),
        mainText = Color(0xFFE1E8F2),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFF7AD7FF),
        subText = Color(0xFFA8B3C5),
    ),
    MOD(
        fill = Color(0xFF252A33),
        pressedFill = Color(0xFF3E4756),
        border = Color(0xFF4F596B),
        mainText = Color(0xFFD6E0EF),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFF7AD7FF),
        subText = Color(0xFFA8B3C5),
    ),
    NUMPAD(
        fill = Color(0xFF484F5E),
        pressedFill = Color(0xFF687286),
        border = Color(0xFF768196),
        mainText = Color(0xFFFFFFFF),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFF7AD7FF),
        subText = Color(0xFFC0CBE0),
    ),
    DARK(
        fill = Color(0xFF1D2027),
        pressedFill = Color(0xFF343A47),
        border = Color(0xFF474E5E),
        mainText = Color(0xFFC8D2E3),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFF7AD7FF),
        subText = Color(0xFF99A4B8),
    ),
    ACCENT_ORANGE(
        fill = Color(0xFFC46A16),
        pressedFill = Color(0xFFE58225),
        border = Color(0xFFF6A84E),
        mainText = Color(0xFFFFFFFF),
        topText = Color(0xFFFFE0A6),
        shiftText = Color(0xFFFFFFFF),
        subText = Color(0xFFFFE0A6),
    ),
    ACCENT_BLUE(
        fill = Color(0xFF245D94),
        pressedFill = Color(0xFF367CC0),
        border = Color(0xFF5DA4E8),
        mainText = Color(0xFFFFFFFF),
        topText = Color(0xFFF5A623),
        shiftText = Color(0xFFBBE5FF),
        subText = Color(0xFFBBE5FF),
    ),
    ACCENT_RED(
        fill = Color(0xFF8F2E2E),
        pressedFill = Color(0xFFB83E3E),
        border = Color(0xFFD96464),
        mainText = Color(0xFFFFFFFF),
        topText = Color(0xFFFFCCCC),
        shiftText = Color(0xFFFFD6D6),
        subText = Color(0xFFFFD6D6),
    );

    companion object {
        fun parse(raw: String): KeyStyle {
            return entries.firstOrNull { it.name == raw } ?: ALPHA
        }
    }
}

data class KeySpec(
    val index: Int,
    val x: Float,
    val y: Float,
    val w: Float,
    val h: Float,
    val nativeKey: Int = 0,
    val czxKey: Int = 0,
    val codepoint: Int = 0,
    val main: String = "",
    val top: String = "",
    val shift: String = "",
    val sub: String = "",
    val style: KeyStyle = KeyStyle.ALPHA,
    val kind: KeyKind = KeyKind.NORMAL,
)

data class KeyboardPage(
    val id: String,
    val label: String,
    val totalWidthU: Float,
    val totalHeightU: Float,
    val keys: List<KeySpec>,
)

data class KeyboardLayout(
    val id: String,
    val name: String,
    val targetIds: List<String>,
    val boardColor: Color,
    val bezelColor: Color,
    val pages: List<KeyboardPage>,
) {
    fun page(index: Int): KeyboardPage {
        if (pages.isEmpty()) {
            return KeyboardPage("main", "MAIN", 1f, 1f, emptyList())
        }
        return pages[index.mod(pages.size)]
    }
}

object KeyboardRepository {
    fun loadAll(context: Context): List<KeyboardLayout> {
        val dir = "keyboards"
        val files = context.assets.list(dir)?.sorted().orEmpty()
        return files.filter { it.endsWith(".json") }.mapNotNull { file ->
            runCatching {
                val text = context.assets.open("$dir/$file").bufferedReader().use { it.readText() }
                parseLayout(JSONObject(text))
            }.getOrNull()
        }
    }

    fun selectForCore(layouts: List<KeyboardLayout>, coreId: String): KeyboardLayout? {
        if (layouts.isEmpty()) return null
        if (coreId.isNotBlank()) {
            layouts.firstOrNull { coreId in it.targetIds }?.let { return it }
        }
        return layouts.firstOrNull { it.id == "pc_g850" } ?: layouts.first()
    }

    private fun parseLayout(obj: JSONObject): KeyboardLayout {
        val targetArr = obj.optJSONArray("targetIds")
        val targetIds = buildList {
            if (targetArr != null) {
                for (i in 0 until targetArr.length()) {
                    add(targetArr.optString(i))
                }
            }
        }

        val pagesArr = obj.optJSONArray("pages")
        val pages = if (pagesArr != null && pagesArr.length() > 0) {
            buildList(pagesArr.length()) {
                for (i in 0 until pagesArr.length()) {
                    val p = pagesArr.getJSONObject(i)
                    add(
                        KeyboardPage(
                            id = p.optString("id", "page_$i"),
                            label = p.optString("label", "P${i + 1}"),
                            totalWidthU = p.getDouble("totalWidthU").toFloat(),
                            totalHeightU = p.getDouble("totalHeightU").toFloat(),
                            keys = parseKeys(p.getJSONArray("keys")),
                        )
                    )
                }
            }
        } else {
            listOf(
                KeyboardPage(
                    id = "main",
                    label = "MAIN",
                    totalWidthU = obj.getDouble("totalWidthU").toFloat(),
                    totalHeightU = obj.getDouble("totalHeightU").toFloat(),
                    keys = parseKeys(obj.getJSONArray("keys")),
                )
            )
        }

        return KeyboardLayout(
            id = obj.getString("id"),
            name = obj.optString("name", obj.getString("id")),
            targetIds = targetIds,
            boardColor = parseColor(obj.optString("boardColor", "#2A2E36")),
            bezelColor = parseColor(obj.optString("bezelColor", "#181B20")),
            pages = pages,
        )
    }

    private fun parseKeys(keysArr: JSONArray): List<KeySpec> {
        return buildList(keysArr.length()) {
            for (i in 0 until keysArr.length()) {
                val k = keysArr.getJSONObject(i)
                val kind = when (k.optString("kind", "NORMAL")) {
                    "MODIFIER_STICKY" -> KeyKind.MODIFIER_STICKY
                    "MODIFIER_TOGGLE" -> KeyKind.MODIFIER_TOGGLE
                    else -> KeyKind.NORMAL
                }
                add(
                    KeySpec(
                        index = i,
                        x = k.getDouble("x").toFloat(),
                        y = k.getDouble("y").toFloat(),
                        w = k.getDouble("w").toFloat(),
                        h = k.getDouble("h").toFloat(),
                        nativeKey = k.optInt("nativeKey", 0),
                        czxKey = k.optInt("czxKey", 0),
                        codepoint = k.optInt("codepoint", 0),
                        main = k.optString("main", ""),
                        top = k.optString("top", ""),
                        shift = k.optString("shift", ""),
                        sub = k.optString("sub", ""),
                        style = KeyStyle.parse(k.optString("style", "ALPHA")),
                        kind = kind,
                    )
                )
            }
        }
    }

    private fun parseColor(hex: String): Color {
        val clean = hex.removePrefix("#")
        val argb = when (clean.length) {
            6 -> 0xFF000000L or clean.toLong(16)
            8 -> clean.toLong(16)
            else -> 0xFF2A2E36L
        }
        return Color(argb.toInt())
    }
}
