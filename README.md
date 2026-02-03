GPStoSPP

GPStoSPP is an Android app that reads your phone’s GPS-derived speed and streams it over Bluetooth Serial Port Profile (SPP / RFCOMM) to a listening device (typically a microcontroller).

This repo is the phone-side transmitter in a two-part system designed to turn GPS speed into a hardware signal a vehicle can understand.

How this relates to the ESP32 project

GPStoSPP pairs with the ESP32 firmware repo:

ESP32 receiver / signal generator: SPPtoVSS
https://github.com/red-peel/SPPtoVSS

Data flow (full system):

Android (this repo): calculates speed from GPS and transmits it over Bluetooth SPP

ESP32 (SPPtoVSS): receives the speed data and converts it into a pulse output suitable for a vehicle’s VSS/ECU/speedometer input

In other words: GPStoSPP is the sensor, SPPtoVSS is the translator.

What it does

Uses GPS location updates to compute speed

Displays current speed in the UI

Streams speed values to a paired Bluetooth device over SPP (RFCOMM “COM port” style)

Expected output format

The ESP32 side expects plain-text speed values sent over the SPP socket.

Typical implementations use:

one value per line (newline-terminated), e.g.:

48.5
49.0
48.7


If you ever change the format here (units, delimiter, JSON, etc.), the ESP32 parser must match.

Requirements

Android device with GPS

Bluetooth enabled

A paired Bluetooth device that supports SPP (classic Bluetooth RFCOMM)

ESP32 typically uses Bluetooth Classic SPP (not BLE) for this

Build & run

Clone:

git clone https://github.com/red-peel/GPStoSPP

Open in Android Studio

Build/Run on a physical device (emulators don’t do real GPS + Bluetooth SPP reliably)

Pair your ESP32/receiver in Android Bluetooth settings

In the app, connect and start streaming

Permissions

This app needs:

Location permission (to read GPS speed)

Bluetooth permissions (to connect + stream)

Android 12+ devices require the newer Bluetooth permission set; older versions use the legacy model. If permissions are denied, you’ll see “connects but no data” style symptoms.

Troubleshooting
“It connects, but the ESP32 gets nothing”

Confirm the ESP32 is advertising/accepting SPP (Bluetooth Classic), not BLE

Confirm you’re connecting to the correct paired device

Confirm both sides agree on the speed format (see above)

“Speed gets weird when the app is in the background”

That’s usually Android being “helpful” with background location updates. Solutions tend to involve:

Foreground service + persistent notification (so the OS keeps location updates flowing)

Requesting higher-accuracy location / proper update intervals

Disabling battery optimizations for the app

(If you want, I can write the Foreground Service implementation cleanly — no duct tape.)

Roadmap ideas

Foreground Service mode for reliable background streaming

Configurable output format (mph/kph, newline vs CSV vs JSON)

Connection status + reconnection logic

Logging / export session data for tuning the ESP32 pulse conversion
