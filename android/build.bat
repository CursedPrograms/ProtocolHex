@echo off
rem Builds the PROTOCOL.hex Android app (debug) and keeps the APK in android\apk\
setlocal
cd /d "%~dp0"
if exist "C:\Users\Cursed\.jdks\jdk-17.0.20.1+1\bin\java.exe" set "JAVA_HOME=C:\Users\Cursed\.jdks\jdk-17.0.20.1+1"
if "%ANDROID_HOME%"=="" if exist "%LOCALAPPDATA%\Android\Sdk" set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
call gradlew.bat assembleDebug
if errorlevel 1 (echo Build failed. & exit /b 1)
echo Done: see the apk folder.
