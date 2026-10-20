package com.micronut99.ares

/** The C++ side: android/native/src/jni.cpp. */
object NativeBridge {
    init { System.loadLibrary("ares_android") }

    /** Machines, as frontend.hpp's aresdroid::Machine. */
    const val AUTO = 0
    const val MEGA_DRIVE = 1
    const val MEGA_32X = 2
    const val MEGA_CD = 3
    const val MEGA_CD_32X = 4

    /** Pad bits, as frontend.hpp's aresdroid::Button. */
    const val UP = 1 shl 0
    const val DOWN = 1 shl 1
    const val LEFT = 1 shl 2
    const val RIGHT = 1 shl 3
    const val A = 1 shl 4
    const val B = 1 shl 5
    const val C = 1 shl 6
    const val X = 1 shl 7
    const val Y = 1 shl 8
    const val Z = 1 shl 9
    const val MODE = 1 shl 10
    const val START = 1 shl 11

    @JvmStatic external fun nativeInit()
    /** Returns null on success, else a message for the user. */
    @JvmStatic external fun nativeLoad(
        home: String, game: String, disc: String, machine: Int, region: String,
        biosUS: String, biosJP: String, biosEU: String,
        ramCartFile: String, ramCartSize: Int, writeProtect: Boolean, recompiler: Boolean,
    ): String?
    @JvmStatic external fun nativeUnload()
    @JvmStatic external fun nativeSetPaused(paused: Boolean)
    @JvmStatic external fun nativeSetFastForward(enabled: Boolean)
    @JvmStatic external fun nativeSetButtons(port: Int, mask: Int)
    @JvmStatic external fun nativeReset()
    @JvmStatic external fun nativeSaveMemory()
    @JvmStatic external fun nativeSaveState(file: String): Boolean
    @JvmStatic external fun nativeLoadState(file: String): Boolean
    @JvmStatic external fun nativeStatus(): String
    @JvmStatic external fun nativeDescription(): String
    @JvmStatic external fun nativeTakeLog(): String
    @JvmStatic external fun nativeSurfaceCreated()
    @JvmStatic external fun nativeSurfaceChanged(width: Int, height: Int)
    @JvmStatic external fun nativeDraw()
    @JvmStatic external fun nativeSetVideo(scale: Int, smooth: Boolean)
}
