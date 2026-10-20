package com.micronut99.ares

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.view.ViewGroup
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Toast
import java.io.File

class SettingsActivity : Activity() {
    private lateinit var prefs: Prefs
    private lateinit var column: LinearLayout

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        build()
    }

    private fun build() {
        column = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(32, 16, 32, 48) }
        setContentView(ScrollView(this).apply { addView(column) })

        section("Sega CD BIOS")
        note("Choose your own BIOS dumps. The 32X boot ROMs are built in.")
        biosRow("US (NTSC-U)", { prefs.biosUS }, { prefs.biosUS = it })
        biosRow("Japan (NTSC-J)", { prefs.biosJP }, { prefs.biosJP = it })
        biosRow("Europe (PAL)", { prefs.biosEU }, { prefs.biosEU = it })
        choiceRow("Region", listOf("" to "From the game (prefer US)", "NTSC-U" to "US", "NTSC-J" to "Japan", "PAL" to "Europe"),
            { prefs.region }, { prefs.region = it })

        section("RAM cart (Sega CD 32X)")
        note("16 MB battery-backed SRAM cart in the cartridge slot (the paged 16MB-PCB-CD32X: page registers " +
            "0xA130F0 / 0xA130F2). Carts 1-9 are separate files, \"RAM Cart 16 MB-<n>.ram\" in " +
            "${prefs.systems.absolutePath}, the same files as Windows ares.")
        choiceRow("Cart in the slot", (1..9).map { n -> n to "Cart $n" + if (prefs.ramCartFile(n).exists()) "  ●" else "  (blank)" },
            { prefs.ramCartSlot }, { prefs.ramCartSlot = it })
        check("Write-protect", { prefs.ramCartWriteProtect }, { prefs.ramCartWriteProtect = it })
        action("Wipe a RAM cart…") {
            val slots = (1..9).filter { prefs.ramCartFile(it).exists() }
            if (slots.isEmpty()) { toast("No RAM cart file yet"); return@action }
            AlertDialog.Builder(this).setTitle("Wipe which cart?")
                .setItems(slots.map { "Cart $it  (${prefs.ramCartFile(it).name})" }.toTypedArray()) { _, which ->
                    val f = prefs.ramCartFile(slots[which])
                    AlertDialog.Builder(this).setTitle("Wipe ${f.name}?")
                        .setMessage("Everything the games copied into it (and the save area at 0x3F0000) is lost.")
                        .setPositiveButton("Wipe") { _, _ -> f.delete(); toast("Wiped") }
                        .setNegativeButton("Cancel", null).show()
                }.show()
        }

        section("Emulation")
        check("SH-2 recompiler (faster; off = interpreter)", { prefs.recompiler }, { prefs.recompiler = it })
        check("Show fps and COMM12 STATE", { prefs.showStatus }, { prefs.showStatus = it })

        section("Video")
        choiceRow("Scale", listOf(0 to "Fit", 1 to "Integer", 2 to "Stretch"), { prefs.scale }, { prefs.scale = it })
        check("Smooth (bilinear)", { prefs.smooth }, { prefs.smooth = it })

        section("Controls")
        check("Touch controls", { prefs.touchControls }, { prefs.touchControls = it })
        slider("Touch control opacity", { prefs.touchOpacity }, { prefs.touchOpacity = it })
        check("Vibrate on touch", { prefs.vibrate }, { prefs.vibrate = it })
        note("Controllers: A→B, B→C, X→A, Y→Y, L1→X, R1→Z, Start→Start, Select→Mode (6-button pad, as desktop ares). " +
            "R2 holds fast-forward. During play, Back opens the menu.")

        section("About")
        note("ares v148 for Android, Mega Drive family only (Genesis, Sega CD, 32X, Sega CD 32X), with the CD32X RAM cart. " +
            "ares is ISC-licensed; this build: github.com/MicroNut99/ares_android")
    }

    private fun section(title: String) {
        column.addView(TextView(this).apply {
            text = title; textSize = 16f; typeface = Typeface.DEFAULT_BOLD
            setTextColor(Color.parseColor("#F2A33A")); setPadding(0, 40, 0, 8)
        })
    }

    private fun note(text: String) {
        column.addView(TextView(this).apply { this.text = text; textSize = 12f; setTextColor(Color.parseColor("#9AA0AA")); setPadding(0, 0, 0, 12) })
    }

    private fun row(title: String, value: String, onClick: () -> Unit): TextView {
        val v = TextView(this).apply {
            text = "$title\n$value"; textSize = 15f; setTextColor(Color.WHITE)
            setPadding(0, 18, 0, 18); setOnClickListener { onClick() }
        }
        column.addView(v, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        return v
    }

    private fun biosRow(title: String, get: () -> String, set: (String) -> Unit) {
        lateinit var v: TextView
        fun shown() = get().ifBlank { "Not set: tap to choose" }.let { if (it.contains('/')) File(it).name else it }
        v = row(title, shown()) {
            val start = get().takeIf { it.isNotBlank() }?.let { File(it).parentFile } ?: File(prefs.lastFolder)
            Files.pick(this, "BIOS: $title", start, null) { set(it.absolutePath); v.text = "$title\n${shown()}" }
        }
        v.setOnLongClickListener { set(""); v.text = "$title\n${shown()}"; true }
    }

    private fun <T> choiceRow(title: String, choices: List<Pair<T, String>>, get: () -> T, set: (T) -> Unit) {
        lateinit var v: TextView
        fun shown() = choices.firstOrNull { it.first == get() }?.second ?: choices.first().second
        v = row(title, shown()) {
            AlertDialog.Builder(this).setTitle(title)
                .setSingleChoiceItems(choices.map { it.second }.toTypedArray(), choices.indexOfFirst { it.first == get() }) { d, which ->
                    set(choices[which].first); v.text = "$title\n${shown()}"; d.dismiss()
                }.show()
        }
    }

    private fun check(title: String, get: () -> Boolean, set: (Boolean) -> Unit) {
        column.addView(CheckBox(this).apply {
            text = title; isChecked = get(); setTextColor(Color.WHITE); textSize = 15f
            setPadding(8, 12, 0, 12)
            setOnCheckedChangeListener { _, checked -> set(checked) }
        })
    }

    private fun slider(title: String, get: () -> Int, set: (Int) -> Unit) {
        column.addView(TextView(this).apply { text = title; setTextColor(Color.WHITE); textSize = 15f; setPadding(0, 12, 0, 0) })
        column.addView(SeekBar(this).apply {
            max = 100; progress = get()
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: SeekBar?, p: Int, fromUser: Boolean) { if (fromUser) set(p) }
                override fun onStartTrackingTouch(s: SeekBar?) {}
                override fun onStopTrackingTouch(s: SeekBar?) {}
            })
        })
    }

    private fun action(title: String, onClick: () -> Unit) { row(title, "") { onClick() } }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
}
