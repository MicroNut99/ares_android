package com.micronut99.ares

import android.content.Context
import android.content.SharedPreferences
import android.os.Environment
import java.io.File

/** Settings, and where files live. Everything is under /sdcard/ares/ so it can be copied to and from a PC. */
class Prefs(context: Context) {
    private val sp: SharedPreferences = context.getSharedPreferences("ares", Context.MODE_PRIVATE)

    val base: File get() = File(Environment.getExternalStorageDirectory(), "ares")
    /** Same layout as Windows ares: Systems\ holds the Sega CD system files and the RAM carts. */
    val systems: File get() = File(base, "Systems")
    val states: File get() = File(base, "States")

    fun ramCartFile(sizeMiB: Int) = File(systems, "RAM Cart $sizeMiB MB.ram")

    var biosUS: String get() = sp.getString("biosUS", "")!!; set(v) = sp.edit().putString("biosUS", v).apply()
    var biosJP: String get() = sp.getString("biosJP", "")!!; set(v) = sp.edit().putString("biosJP", v).apply()
    var biosEU: String get() = sp.getString("biosEU", "")!!; set(v) = sp.edit().putString("biosEU", v).apply()

    /** 4 (MicroNut99 RAM cart), 8 or 16 (16MB-PCB-CD32X, paged). */
    var ramCartSize: Int get() = sp.getInt("ramCartSize", 16); set(v) = sp.edit().putInt("ramCartSize", v).apply()
    var ramCartWriteProtect: Boolean get() = sp.getBoolean("ramCartWP", false); set(v) = sp.edit().putBoolean("ramCartWP", v).apply()
    var recompiler: Boolean get() = sp.getBoolean("recompiler", true); set(v) = sp.edit().putBoolean("recompiler", v).apply()
    var region: String get() = sp.getString("region", "")!!; set(v) = sp.edit().putString("region", v).apply()

    /** 0 fit, 1 integer, 2 stretch. */
    var scale: Int get() = sp.getInt("scale", 0); set(v) = sp.edit().putInt("scale", v).apply()
    var smooth: Boolean get() = sp.getBoolean("smooth", true); set(v) = sp.edit().putBoolean("smooth", v).apply()
    var touchControls: Boolean get() = sp.getBoolean("touch", true); set(v) = sp.edit().putBoolean("touch", v).apply()
    var touchOpacity: Int get() = sp.getInt("touchOpacity", 45); set(v) = sp.edit().putInt("touchOpacity", v).apply()
    var showStatus: Boolean get() = sp.getBoolean("status", false); set(v) = sp.edit().putBoolean("status", v).apply()
    var vibrate: Boolean get() = sp.getBoolean("vibrate", true); set(v) = sp.edit().putBoolean("vibrate", v).apply()

    var lastFolder: String
        get() = sp.getString("lastFolder", Environment.getExternalStorageDirectory().absolutePath)!!
        set(v) = sp.edit().putString("lastFolder", v).apply()

    /** Per game: which machine to boot, and a Sega CD disc to go with a cartridge. */
    fun machineFor(game: String): Int = sp.getInt("machine:$game", NativeBridge.AUTO)
    fun setMachineFor(game: String, machine: Int) = sp.edit().putInt("machine:$game", machine).apply()
    fun discFor(game: String): String = sp.getString("disc:$game", "")!!
    fun setDiscFor(game: String, disc: String) = sp.edit().putString("disc:$game", disc).apply()

    var recent: List<String>
        get() = sp.getString("recent", "")!!.split('\n').filter { it.isNotBlank() }
        set(v) = sp.edit().putString("recent", v.take(12).joinToString("\n")).apply()

    fun addRecent(game: String) { recent = listOf(game) + recent.filter { it != game } }
}
