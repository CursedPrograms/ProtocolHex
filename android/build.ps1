# Builds the PROTOCOL.hex Android app (debug) and keeps the APK in android\apk\
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
$jdk = "C:\Users\Cursed\.jdks\jdk-17.0.20.1+1"
if (Test-Path "$jdk\bin\java.exe") { $env:JAVA_HOME = $jdk }
if (-not $env:ANDROID_HOME -and (Test-Path "$env:LOCALAPPDATA\Android\Sdk")) { $env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk" }
& .\gradlew.bat assembleDebug
if ($LASTEXITCODE -ne 0) { Write-Host "Build failed."; exit 1 }
Write-Host "Done: see the apk folder."
