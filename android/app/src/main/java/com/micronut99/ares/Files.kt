package com.micronut99.ares

import android.app.AlertDialog
import android.content.Context
import java.io.File

object Files {
    val gameExtensions = setOf("cue", "chd", "32x", "md", "gen", "bin", "smd", "zip")
    val discExtensions = setOf("cue", "chd")

    /** A .bin next to a .cue of the same name is the disc's data, not a cartridge: hide it. */
    fun listGames(folder: File, extensions: Set<String>): List<File> {
        val all = folder.listFiles()?.toList() ?: return emptyList()
        val cueStems = all.filter { it.extension.equals("cue", true) }.map { it.nameWithoutExtension.lowercase() }.toSet()
        val cueReferenced = all.filter { it.extension.equals("cue", true) }
            .flatMap { cue -> runCatching { cue.readLines() }.getOrDefault(emptyList()) }
            .mapNotNull { Regex("FILE\\s+\"?([^\"]+?)\"?\\s+\\w+\\s*$", RegexOption.IGNORE_CASE).find(it.trim())?.groupValues?.get(1)?.lowercase() }
            .toSet()
        val dirs = all.filter { it.isDirectory && !it.name.startsWith(".") }.sortedBy { it.name.lowercase() }
        val files = all.filter { f ->
            f.isFile && f.extension.lowercase() in extensions &&
                !(f.extension.equals("bin", true) && (f.nameWithoutExtension.lowercase() in cueStems || f.name.lowercase() in cueReferenced))
        }.sortedBy { it.name.lowercase() }
        return dirs + files
    }

    /** A simple folder browser in a dialog. */
    fun pick(context: Context, title: String, start: File, extensions: Set<String>?, onPicked: (File) -> Unit) {
        var folder = if (start.isDirectory) start else (start.parentFile ?: File("/sdcard"))
        fun show() {
            val entries = mutableListOf<File?>()
            val labels = mutableListOf<String>()
            folder.parentFile?.let { entries += it; labels += "⬆  .." }
            val list = folder.listFiles()?.filter { !it.name.startsWith(".") }?.sortedWith(compareBy({ !it.isDirectory }, { it.name.lowercase() })) ?: emptyList()
            for (f in list) {
                if (f.isDirectory) { entries += f; labels += "📁  ${f.name}" }
                else if (extensions == null || f.extension.lowercase() in extensions) { entries += f; labels += f.name }
            }
            AlertDialog.Builder(context)
                .setTitle("$title\n${folder.absolutePath}")
                .setItems(labels.toTypedArray()) { _, which ->
                    val f = entries[which]!!
                    if (f.isDirectory) { folder = f; show() } else onPicked(f)
                }
                .setNegativeButton("Cancel", null)
                .show()
        }
        show()
    }
}
