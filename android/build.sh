#!/usr/bin/env bash
# Builds the PROTOCOL.hex Android app (debug) and keeps the APK in android/apk/
set -e
cd "$(dirname "$0")"
if [ -z "$JAVA_HOME" ] && [ -x "$HOME/.jdks/jdk-17.0.20.1+1/bin/java" ]; then export JAVA_HOME="$HOME/.jdks/jdk-17.0.20.1+1"; fi
if [ -z "$ANDROID_HOME" ]; then
  for d in "$HOME/Android/Sdk" "$HOME/Library/Android/sdk" "$LOCALAPPDATA/Android/Sdk"; do
    if [ -d "$d" ]; then export ANDROID_HOME="$d"; break; fi
  done
fi
chmod +x gradlew
./gradlew assembleDebug
echo "Done: see the apk folder."
