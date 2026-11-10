// PROTOCOL.hex: the browser game in the repo root, played offline in a full-screen WebView.
// The web files are copied into the app's assets at build time, so the browser game stays the only copy.
import java.time.LocalDate

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val gameLabel = "PROTOCOL.hex"
val apkName = "PROTOCOL.hex"
val gameRoot = rootProject.projectDir.parentFile          // the repo root, where index.html lives
val gameAssets = layout.buildDirectory.dir("generated/gameAssets")

android {
    namespace = "com.synthwomb.protocolhex"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.synthwomb.protocolhex"
        minSdk = 24
        targetSdk = 34
        versionCode = 1
        versionName = "1.0"
        resValue("string", "app_name", gameLabel)
    }
    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions {
        jvmTarget = "17"
    }
    sourceSets["main"].assets.srcDir(gameAssets)
}

// Copy the browser game into assets/game. Not the C++ exe, the Android app, git, docs or screenshots.
val syncGameAssets by tasks.registering(Sync::class) {
    from(gameRoot) {
        exclude(
            "cpp/**", "android/**", ".git/**", ".github/**", ".vscode/**", ".idea/**", "node_modules/**",
            "**/*.md", "LICENSE", "link.bf", "netlify.toml", "config.json", "**/*.bat", "**/*.sh", "**/*.ps1",
            "images/screenshot-*", "images/exe-*"
        
        )
    }
    into(gameAssets.map { it.dir("game") })
}
tasks.named("preBuild") { dependsOn(syncGameAssets) }

// After assembleDebug, keep the APK in the repo: android/apk/<name>-debug-<yyyy-mm-dd>.apk (only the newest).
tasks.matching { it.name == "assembleDebug" }.configureEach {
    doLast {
        val apkDir = rootProject.file("apk")
        apkDir.mkdirs()
        apkDir.listFiles { f -> f.name.startsWith("$apkName-debug-") && f.name.endsWith(".apk") }?.forEach { it.delete() }
        val out = File(apkDir, "$apkName-debug-${LocalDate.now()}.apk")
        layout.buildDirectory.file("outputs/apk/debug/app-debug.apk").get().asFile.copyTo(out, overwrite = true)
        println("APK: " + out)
    }
}
