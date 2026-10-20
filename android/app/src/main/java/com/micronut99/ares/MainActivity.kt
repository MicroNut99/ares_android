package com.micronut99.ares

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.graphics.Typeface
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import java.io.File

/** The game browser. Tap a game to play it; long-press for how to boot it. */
class MainActivity : Activity() {
    private lateinit var prefs: Prefs
    private lateinit var header: TextView
    private lateinit var warning: TextView
    private lateinit var list: ListView
    private var folder = File("/sdcard")
    private var entries: List<File> = emptyList()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)

        val root = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        warning = TextView(this).apply {
            setPadding(32, 20, 32, 20)
            setBackgroundColor(Color.parseColor("#5A3A12"))
            setTextColor(Color.WHITE)
            visibility = View.GONE
            setOnClickListener { startActivity(Intent(this@MainActivity, SettingsActivity::class.java)) }
        }
        header = TextView(this).apply {
            setPadding(32, 20, 32, 20)
            setTextColor(Color.parseColor("#B0B6C0"))
            textSize = 13f
        }
        list = ListView(this)
        root.addView(warning, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(header, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        root.addView(list, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        setContentView(root)

        list.setOnItemClickListener { _, _, position, _ -> open(entries[position]) }
        list.setOnItemLongClickListener { _, _, position, _ ->
            val f = entries[position]
            if (f.isFile) gameOptions(f)
            true
        }

        folder = File(prefs.lastFolder).takeIf { it.isDirectory } ?: Environment.getExternalStorageDirectory()
    }

    override fun onResume() {
        super.onResume()
        if (!hasStorage()) { askStorage(); return }
        refresh()
    }

    private fun hasStorage(): Boolean =
        if (Build.VERSION.SDK_INT >= 30) Environment.isExternalStorageManager()
        else checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED

    private fun askStorage() {
        AlertDialog.Builder(this)
            .setTitle("Allow access to your games")
            .setMessage("A .cue file names its .bin files by path, so ares needs to read the folders your games are in, " +
                "and it keeps the RAM cart, BIOS settings and saves in /sdcard/ares/.\n\nOn the next screen, turn on " +
                "\"Allow access to manage all files\" for ares CD32X.")
            .setCancelable(false)
            .setPositiveButton("Continue") { _, _ ->
                if (Build.VERSION.SDK_INT >= 30) {
                    try {
                        startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
                    } catch (e: Exception) {
                        startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                    }
                } else {
                    requestPermissions(arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE), 1)
                }
            }
            .show()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        if (hasStorage()) refresh()
    }

    private fun refresh() {
        prefs.systems.mkdirs()
        prefs.states.mkdirs()
        val missing = mutableListOf<String>()
        if (prefs.biosUS.isBlank() && prefs.biosJP.isBlank() && prefs.biosEU.isBlank()) missing += "No Sega CD BIOS set: tap here to choose it (Settings → BIOS)."
        warning.text = missing.joinToString("\n")
        warning.visibility = if (missing.isEmpty()) View.GONE else View.VISIBLE

        entries = Files.listGames(folder, Files.gameExtensions)
        header.text = folder.absolutePath
        val labels = entries.map { if (it.isDirectory) "📁  ${it.name}" else label(it) }
        list.adapter = object : ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, labels) {
            override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                val v = super.getView(position, convertView, parent) as TextView
                v.setTextColor(if (entries[position].isDirectory) Color.parseColor("#F2A33A") else Color.WHITE)
                v.typeface = if (entries[position].isDirectory) Typeface.DEFAULT_BOLD else Typeface.DEFAULT
                return v
            }
        }
        prefs.lastFolder = folder.absolutePath
    }

    private fun label(game: File): String {
        val machine = machineFor(game)
        val disc = prefs.discFor(game.absolutePath)
        val tag = when (machine) {
            NativeBridge.MEGA_CD_32X -> "CD32X + ${prefs.ramCartSize} MB cart"
            NativeBridge.MEGA_CD -> "Sega CD"
            NativeBridge.MEGA_32X -> if (disc.isNotBlank()) "32X + CD" else "32X"
            else -> if (disc.isNotBlank()) "Genesis + CD" else "Genesis"
        }
        return "${game.name}\n      $tag"
    }

    /** What Auto means for this file (as frontend.cpp's guessMachine). */
    private fun machineFor(game: File): Int {
        val chosen = prefs.machineFor(game.absolutePath)
        if (chosen != NativeBridge.AUTO) return chosen
        return when (game.extension.lowercase()) {
            "cue", "chd" -> NativeBridge.MEGA_CD_32X
            "32x" -> NativeBridge.MEGA_32X
            else -> NativeBridge.MEGA_DRIVE
        }
    }

    private fun open(f: File) {
        if (f.isDirectory) { folder = f; refresh(); return }
        val intent = Intent(this, EmulationActivity::class.java)
            .putExtra(EmulationActivity.EXTRA_GAME, f.absolutePath)
            .putExtra(EmulationActivity.EXTRA_MACHINE, machineFor(f))
            .putExtra(EmulationActivity.EXTRA_DISC, prefs.discFor(f.absolutePath))
        prefs.addRecent(f.absolutePath)
        startActivity(intent)
    }

    private fun gameOptions(game: File) {
        val isDisc = game.extension.lowercase() in Files.discExtensions
        val disc = prefs.discFor(game.absolutePath)
        val items = mutableListOf("Play", "Boot as: ${machineName(machineFor(game))}")
        if (!isDisc) items += if (disc.isBlank()) "Add a Sega CD disc (CD audio)…" else "Remove disc ${File(disc).name}"
        AlertDialog.Builder(this)
            .setTitle(game.name)
            .setItems(items.toTypedArray()) { _, which ->
                when (which) {
                    0 -> open(game)
                    1 -> chooseMachine(game, isDisc)
                    2 -> if (disc.isBlank()) {
                        Files.pick(this, "Sega CD disc for ${game.name}", game.parentFile ?: folder, Files.discExtensions) {
                            prefs.setDiscFor(game.absolutePath, it.absolutePath); refresh()
                        }
                    } else { prefs.setDiscFor(game.absolutePath, ""); refresh() }
                }
            }
            .show()
    }

    private fun chooseMachine(game: File, isDisc: Boolean) {
        val machines = if (isDisc) listOf(NativeBridge.MEGA_CD_32X, NativeBridge.MEGA_CD)
                       else listOf(NativeBridge.MEGA_32X, NativeBridge.MEGA_DRIVE)
        AlertDialog.Builder(this)
            .setTitle("Boot ${game.name} as")
            .setItems(machines.map { machineName(it) }.toTypedArray()) { _, which ->
                prefs.setMachineFor(game.absolutePath, machines[which]); refresh()
            }
            .show()
    }

    private fun machineName(m: Int) = when (m) {
        NativeBridge.MEGA_CD_32X -> "Sega CD 32X + RAM cart"
        NativeBridge.MEGA_CD -> "Sega CD"
        NativeBridge.MEGA_32X -> "32X"
        NativeBridge.MEGA_DRIVE -> "Genesis / Mega Drive"
        else -> "Auto"
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val parent = folder.parentFile
        if (parent != null && parent.canRead() && folder.absolutePath != "/") { folder = parent; refresh() }
        else @Suppress("DEPRECATION") super.onBackPressed()
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menu.add(0, 1, 0, "Recent").setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        menu.add(0, 2, 0, "Settings").setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
        menu.add(0, 3, 0, "Go to /sdcard")
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            1 -> {
                val recent = prefs.recent.map { File(it) }.filter { it.exists() }
                AlertDialog.Builder(this).setTitle("Recent")
                    .setItems(recent.map { it.name }.toTypedArray()) { _, which -> open(recent[which]) }
                    .show()
            }
            2 -> startActivity(Intent(this, SettingsActivity::class.java))
            3 -> { folder = Environment.getExternalStorageDirectory(); refresh() }
        }
        return true
    }
}
