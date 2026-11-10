package com.synthwomb.protocolhex

import android.annotation.SuppressLint
import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import java.io.ByteArrayInputStream
import java.io.FileNotFoundException

/**
 * PROTOCOL.hex, an Arcade Phantasm game: the browser game, bundled in assets/game and shown full screen.
 * The files are served from https://appassets.androidplatform.net/ so the page has a real origin
 * (localStorage keeps the best scores) and no network is needed.
 */
class MainActivity : Activity() {

    private lateinit var web: WebView
    private var lastBack = 0L

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= 28) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }

        web = WebView(this)
        web.setBackgroundColor(Color.BLACK)
        web.overScrollMode = View.OVER_SCROLL_NEVER
        web.isVerticalScrollBarEnabled = false
        web.isHorizontalScrollBarEnabled = false
        web.isHapticFeedbackEnabled = false
        web.setOnLongClickListener { true }          // no text-selection menu on long presses
        web.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            mediaPlaybackRequiresUserGesture = false  // music and sound without a first tap
            textZoom = 100
            setSupportZoom(false)
            builtInZoomControls = false
            displayZoomControls = false
            allowFileAccess = false
            allowContentAccess = false
        }
        web.webViewClient = AssetClient()
        setContentView(web)
        immersive()

        if (savedInstanceState == null || web.restoreState(savedInstanceState) == null) {
            web.loadUrl(START)
        }
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        web.saveState(outState)
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) immersive()
    }

    override fun onPause() {
        web.evaluateJavascript("window.__appPause && window.__appPause()", null)
        web.onPause()
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        web.onResume()
        web.evaluateJavascript("window.__appResume && window.__appResume()", null)
        immersive()
    }

    override fun onDestroy() {
        web.destroy()
        super.onDestroy()
    }

    // Back pauses the game (as losing focus does in the browser); a second Back within two seconds leaves.
    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        val now = System.currentTimeMillis()
        if (now - lastBack < 2000) {
            @Suppress("DEPRECATION")
            super.onBackPressed()
            return
        }
        lastBack = now
        // the games pause themselves when the window loses focus
        web.evaluateJavascript("window.dispatchEvent(new Event('blur'))", null)
        Toast.makeText(this, "Press Back again to leave", Toast.LENGTH_SHORT).show()
    }

    @Suppress("DEPRECATION")
    private fun immersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            window.decorView.systemUiVisibility = (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                or View.SYSTEM_UI_FLAG_FULLSCREEN
                or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN)
        }
    }

    /** Serves assets/game/... for https://appassets.androidplatform.net/...; everything else is blocked. */
    private inner class AssetClient : WebViewClient() {
        override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
            val url = request.url
            if (url.host != HOST) {
                return WebResourceResponse("text/plain", "utf-8", 404, "Offline", null, ByteArrayInputStream(ByteArray(0)))
            }
            var path = (url.path ?: "/").trimStart('/')
            if (path.isEmpty() || path.endsWith("/")) path += "index.html"
            val type = mime(path)
            return try {
                val stream = assets.open("game/$path")
                val headers = mapOf("Cache-Control" to "no-cache", "Access-Control-Allow-Origin" to "*")
                if (path == "index.html") {
                    // the pause/resume hook goes in before the game's own scripts
                    val html = stream.bufferedReader().use { it.readText() }
                    val i = html.indexOf("<head>", ignoreCase = true)
                    val out = if (i >= 0) html.substring(0, i + 6) + SHIM + html.substring(i + 6) else SHIM + html
                    WebResourceResponse(type, "utf-8", 200, "OK", headers, ByteArrayInputStream(out.toByteArray()))
                } else {
                    WebResourceResponse(type, if (type.startsWith("text/") || type.endsWith("javascript")) "utf-8" else null,
                        200, "OK", headers, stream)
                }
            } catch (e: FileNotFoundException) {
                WebResourceResponse("text/plain", "utf-8", 404, "Not Found", null, ByteArrayInputStream(ByteArray(0)))
            }
        }

        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean =
            request.url.host != HOST
    }

    companion object {
        private const val HOST = "appassets.androidplatform.net"
        private const val START = "https://$HOST/index.html"

        // Remembers every AudioContext so leaving the app can hush them, and pauses <audio> too.
        private const val SHIM = """<script>(function(){var cs=[];var A=window.AudioContext||window.webkitAudioContext;
if(A){var W=function(o){var c=o===undefined?new A():new A(o);cs.push(c);return c;};W.prototype=A.prototype;
window.AudioContext=W;window.webkitAudioContext=W;}
window.__appPause=function(){try{window.dispatchEvent(new Event('blur'));}catch(e){}cs.forEach(function(c){if(c.state==='running'){c.__on=1;c.suspend();}});
document.querySelectorAll('audio,video').forEach(function(m){if(!m.paused){m.__on=1;m.pause();}});};
window.__appResume=function(){cs.forEach(function(c){if(c.__on){c.__on=0;c.resume();}});
document.querySelectorAll('audio,video').forEach(function(m){if(m.__on){m.__on=0;var p=m.play();if(p&&p.catch)p.catch(function(){});}});};
})();</script>"""

        private fun mime(path: String): String = when (path.substringAfterLast('.', "").lowercase()) {
            "html", "htm" -> "text/html"
            "js", "mjs" -> "application/javascript"
            "css" -> "text/css"
            "json" -> "application/json"
            "png" -> "image/png"
            "jpg", "jpeg" -> "image/jpeg"
            "gif" -> "image/gif"
            "webp" -> "image/webp"
            "svg" -> "image/svg+xml"
            "ico" -> "image/x-icon"
            "mp3" -> "audio/mpeg"
            "ogg", "oga" -> "audio/ogg"
            "wav" -> "audio/wav"
            "m4a" -> "audio/mp4"
            "mp4" -> "video/mp4"
            "webm" -> "video/webm"
            "woff" -> "font/woff"
            "woff2" -> "font/woff2"
            "ttf" -> "font/ttf"
            "otf" -> "font/otf"
            "txt" -> "text/plain"
            else -> "application/octet-stream"
        }
    }
}
