# OrcSDR Android TV

Living-room Mission Control client for the Tab5 LAN web console.
The TV is a 10-foot RF visualization surface. The Tab5 remains the radio.

Validated target from the bench TV:

- Product `V8-R851T02-LF1V449.018073` (Realtek Android TV)
- Android 9 / API 28, security patch 2020-09-05
- USB debugging enabled
- TV LAN address `192.168.1.184` (this is the TV, not the Tab5)

This bench APK opens `http://192.168.1.75/` directly (the Tab5 STA address).
The connect/IP pad is unused on this TV because Android 9 D-pad text entry
would not save. Change `ConsoleActivity.TAB5_HOST` if the Tab5 IP moves.

## Build and sideload

USB debugging is already on. Plug the TV into this PC, or use TCP after `adb tcpip 5555`:

```powershell
Set-Location F:\Ai\OrcSDR-native-lora-decoder\apps\orcsdr-tv
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
& "$env:USERPROFILE\.gradle\wrapper\dists\gradle-8.11.1-bin\bpt9gzteqjrbo1mjrsomdt32c\gradle-8.11.1\bin\gradle.bat" :app:assembleDebug
adb install -r .\app\build\outputs\apk\debug\app-debug.apk
```

If the TV is only on Wi-Fi:

```powershell
adb connect 192.168.1.184:5555
adb devices
```

Android 9 needs a one-time USB `adb tcpip 5555` before wireless connect will stick.

## Use

1. Flash firmware that includes `web_console` and connect the Tab5 to Wi-Fi.
2. Unplug the PC USB Serial/JTAG flashing cable (`COM17` on this bench).
   Leaving it connected can brownout the Tab5 during Wi-Fi + RTL receive.
   The TV talks to the Tab5 over LAN, not that cable.
3. Settings → Companion → ENABLE. Note `http://<tab5-ip>/`.
4. Open OrcSDR on the TV. This bench APK already points at `192.168.1.75`.
5. Back leaves the console.
