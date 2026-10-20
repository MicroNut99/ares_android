package com.micronut99.ares

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.graphics.Color
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.FrameLayout
import android.widget.TextView
import android.widget.Toast
import java.io.File
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.concurrent.thread

class EmulationActivity : Activity() {
    companion object {
        const val EXTRA_GAME = "game"
        const val EXTRA_DISC = "disc"
        const val EXTRA_MACHINE = "machine"
    }

    private lateinit var prefs: Prefs
    private lateinit var glView: GLSurfaceView
    private lateinit var touch: TouchPadView
    private lateinit var status: TextView
    private val handler = Handler(Looper.getMainLooper())

    private var game = ""
    private var loaded = false
    private var menuOpen = false
    private var fastForward = false
    private var touchMask = 0
    private var keyMask = 0
    private var hatMask = 0
    private var stickMask = 0
    private var slot = 1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        prefs = Prefs(this)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        NativeBridge.nativeInit()

        glView = GLSurfaceView(this).apply {
            setEGLContextClientVersion(2)
            setRenderer(object : GLSurfaceView.Renderer {
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) = NativeBridge.nativeSurfaceCreated()
                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) = NativeBridge.nativeSurfaceChanged(width, height)
                override fun onDrawFrame(gl: GL10?) = NativeBridge.nativeDraw()
            })
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        touch = TouchPadView(this) { mask -> touchMask = mask; sendButtons() }
        status = TextView(this).apply {
            setTextColor(Color.YELLOW); textSize = 12f; setShadowLayer(3f, 1f, 1f, Color.BLACK); setPadding(16, 8, 16, 8)
        }
        val menuButton = TextView(this).apply {
            text = "☰"; textSize = 26f; setTextColor(Color.argb(170, 255, 255, 255)); setPadding(28, 8, 28, 8)
            setOnClickListener { showMenu() }
        }
        val root = FrameLayout(this)
        root.addView(glView, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(touch, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        root.addView(status, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.START))
        root.addView(menuButton, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP or Gravity.END))
        setContentView(root)

        startGame(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        if (intent.getStringExtra(EXTRA_GAME) != game) startGame(intent)
    }

    private fun startGame(intent: Intent) {
        game = intent.getStringExtra(EXTRA_GAME) ?: return finish()
        val disc = intent.getStringExtra(EXTRA_DISC) ?: ""
        val machine = intent.getIntExtra(EXTRA_MACHINE, NativeBridge.AUTO)
        loaded = false
        status.text = "Loading ${File(game).name}…"
        prefs.systems.mkdirs()
        val home = prefs.systems.absolutePath + "/"
        val cartSize = prefs.ramCartSize
        thread(name = "ares-load") {
            val error = NativeBridge.nativeLoad(
                home, game, disc, machine, prefs.region,
                prefs.biosUS, prefs.biosJP, prefs.biosEU,
                prefs.ramCartFile(cartSize).absolutePath, cartSize, prefs.ramCartWriteProtect, prefs.recompiler,
            )
            runOnUiThread {
                if (error != null) {
                    AlertDialog.Builder(this).setTitle("Can't start ${File(game).name}").setMessage(error)
                        .setPositiveButton("OK") { _, _ -> finish() }.setCancelable(false).show()
                } else {
                    loaded = true
                    Toast.makeText(this, NativeBridge.nativeDescription(), Toast.LENGTH_SHORT).show()
                    if (!menuOpen) NativeBridge.nativeSetPaused(false)
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        hideSystemUi()
        glView.onResume()
        touch.visibility = if (prefs.touchControls) View.VISIBLE else View.GONE
        touch.opacity = prefs.touchOpacity / 100f
        touch.vibrate = prefs.vibrate
        NativeBridge.nativeSetVideo(prefs.scale, prefs.smooth)
        if (loaded && !menuOpen) NativeBridge.nativeSetPaused(false)
        handler.post(statusTick)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(statusTick)
        NativeBridge.nativeSetPaused(true)
        glView.onPause()
        //battery memory to disk whenever the app leaves the screen: the RAM cart, cart SRAM, Sega CD backup RAM
        if (loaded) thread(name = "ares-save") { NativeBridge.nativeSaveMemory() }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (isFinishing) {
            loaded = false
            NativeBridge.nativeUnload()  //also writes the RAM cart
        }
    }

    private val statusTick = object : Runnable {
        override fun run() {
            status.visibility = if (prefs.showStatus || !loaded) View.VISIBLE else View.GONE
            if (loaded) status.text = NativeBridge.nativeStatus() + if (fastForward) "  ▶▶" else ""
            handler.postDelayed(this, 500)
        }
    }

    private fun hideSystemUi() {
        if (android.os.Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) hideSystemUi()
    }

    // --- input ------------------------------------------------------------------------------

    private fun sendButtons() = NativeBridge.nativeSetButtons(0, touchMask or keyMask or hatMask or stickMask)

    /** The 6-button pad on a modern controller, as desktop ares maps it (west = A, south = B, east = C). */
    private fun bitFor(keyCode: Int): Int = when (keyCode) {
        KeyEvent.KEYCODE_DPAD_UP -> NativeBridge.UP
        KeyEvent.KEYCODE_DPAD_DOWN -> NativeBridge.DOWN
        KeyEvent.KEYCODE_DPAD_LEFT -> NativeBridge.LEFT
        KeyEvent.KEYCODE_DPAD_RIGHT -> NativeBridge.RIGHT
        KeyEvent.KEYCODE_BUTTON_X -> NativeBridge.A
        KeyEvent.KEYCODE_BUTTON_A -> NativeBridge.B
        KeyEvent.KEYCODE_BUTTON_B -> NativeBridge.C
        KeyEvent.KEYCODE_BUTTON_L1 -> NativeBridge.X
        KeyEvent.KEYCODE_BUTTON_Y -> NativeBridge.Y
        KeyEvent.KEYCODE_BUTTON_R1 -> NativeBridge.Z
        KeyEvent.KEYCODE_BUTTON_START, KeyEvent.KEYCODE_ENTER -> NativeBridge.START
        KeyEvent.KEYCODE_BUTTON_SELECT -> NativeBridge.MODE
        //a keyboard, for Chromebooks and phones with one attached
        KeyEvent.KEYCODE_W -> NativeBridge.UP
        KeyEvent.KEYCODE_S -> NativeBridge.DOWN
        KeyEvent.KEYCODE_A -> NativeBridge.LEFT
        KeyEvent.KEYCODE_D -> NativeBridge.RIGHT
        KeyEvent.KEYCODE_J -> NativeBridge.A
        KeyEvent.KEYCODE_K -> NativeBridge.B
        KeyEvent.KEYCODE_L -> NativeBridge.C
        KeyEvent.KEYCODE_U -> NativeBridge.X
        KeyEvent.KEYCODE_I -> NativeBridge.Y
        KeyEvent.KEYCODE_O -> NativeBridge.Z
        KeyEvent.KEYCODE_SPACE -> NativeBridge.MODE
        else -> 0
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (menuOpen) return super.dispatchKeyEvent(event)
        val code = event.keyCode
        if (code == KeyEvent.KEYCODE_BACK || code == KeyEvent.KEYCODE_BUTTON_MODE || code == KeyEvent.KEYCODE_ESCAPE) {
            if (event.action == KeyEvent.ACTION_UP) showMenu()
            return true
        }
        if (code == KeyEvent.KEYCODE_BUTTON_R2 || code == KeyEvent.KEYCODE_TAB) {
            setFastForward(event.action == KeyEvent.ACTION_DOWN)
            return true
        }
        val bit = bitFor(code)
        if (bit == 0) return super.dispatchKeyEvent(event)
        keyMask = if (event.action == KeyEvent.ACTION_DOWN) keyMask or bit else keyMask and bit.inv()
        sendButtons()
        return true
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        val joystick = (event.source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK ||
            (event.source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
        if (!joystick || event.action != MotionEvent.ACTION_MOVE || menuOpen) return super.dispatchGenericMotionEvent(event)
        fun dir(value: Float, neg: Int, pos: Int, dead: Float) = if (value < -dead) neg else if (value > dead) pos else 0
        hatMask = dir(event.getAxisValue(MotionEvent.AXIS_HAT_X), NativeBridge.LEFT, NativeBridge.RIGHT, 0.5f) or
            dir(event.getAxisValue(MotionEvent.AXIS_HAT_Y), NativeBridge.UP, NativeBridge.DOWN, 0.5f)
        stickMask = dir(event.getAxisValue(MotionEvent.AXIS_X), NativeBridge.LEFT, NativeBridge.RIGHT, 0.45f) or
            dir(event.getAxisValue(MotionEvent.AXIS_Y), NativeBridge.UP, NativeBridge.DOWN, 0.45f)
        val trigger = maxOf(event.getAxisValue(MotionEvent.AXIS_RTRIGGER), event.getAxisValue(MotionEvent.AXIS_GAS))
        if ((trigger > 0.5f) != fastForward) setFastForward(trigger > 0.5f)
        sendButtons()
        return true
    }

    private fun setFastForward(on: Boolean) {
        fastForward = on
        NativeBridge.nativeSetFastForward(on)
    }

    // --- menu -------------------------------------------------------------------------------

    private fun stateFile(n: Int): File {
        prefs.states.mkdirs()
        return File(prefs.states, "${File(game).nameWithoutExtension}.state$n")
    }

    private fun showMenu() {
        if (menuOpen) return
        menuOpen = true
        NativeBridge.nativeSetPaused(true)
        keyMask = 0; hatMask = 0; stickMask = 0; sendButtons()
        val items = arrayOf(
            "Resume",
            "Save state (slot $slot)",
            "Load state (slot $slot)",
            "State slot…",
            if (fastForward) "Fast-forward: on" else "Fast-forward: off",
            if (prefs.touchControls) "Hide touch controls" else "Show touch controls",
            "Reset",
            "Log",
            "Quit",
        )
        AlertDialog.Builder(this)
            .setTitle(NativeBridge.nativeDescription().ifBlank { File(game).name })
            .setItems(items) { _, which ->
                when (which) {
                    1 -> { val ok = NativeBridge.nativeSaveState(stateFile(slot).absolutePath); toast(if (ok) "Saved slot $slot" else "Save failed") }
                    2 -> {
                        val f = stateFile(slot)
                        val ok = f.exists() && NativeBridge.nativeLoadState(f.absolutePath)
                        toast(if (ok) "Loaded slot $slot" else "No state in slot $slot")
                    }
                    3 -> {
                        AlertDialog.Builder(this).setTitle("State slot")
                            .setItems((1..9).map { n -> "Slot $n" + if (stateFile(n).exists()) "  ●" else "" }.toTypedArray()) { _, i -> slot = i + 1 }
                            .setOnDismissListener { closeMenu(); showMenu() }.show()
                        return@setItems
                    }
                    4 -> setFastForward(!fastForward)
                    5 -> {
                        prefs.touchControls = !prefs.touchControls
                        touch.visibility = if (prefs.touchControls) View.VISIBLE else View.GONE
                    }
                    6 -> NativeBridge.nativeReset()
                    7 -> {
                        AlertDialog.Builder(this).setTitle("Log").setMessage(NativeBridge.nativeTakeLog().ifBlank { "(empty)" })
                            .setPositiveButton("OK", null).setOnDismissListener { closeMenu() }.show()
                        return@setItems
                    }
                    8 -> { finish(); return@setItems }
                }
                closeMenu()
            }
            .setOnCancelListener { closeMenu() }
            .show()
    }

    private fun closeMenu() {
        menuOpen = false
        hideSystemUi()
        if (loaded && !isFinishing) NativeBridge.nativeSetPaused(false)
    }

    private fun toast(text: String) = Toast.makeText(this, text, Toast.LENGTH_SHORT).show()
}
