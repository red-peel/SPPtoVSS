# SPPtoVSS

**SPPtoVSS** is an ESP32 firmware project that converts real-time speed data received over **Bluetooth Classic (SPP)** into a **vehicle speed sensor (VSS)–compatible signal**.

This repository represents the **hardware-facing half** of a two-part system.  
It listens for speed data sent wirelessly from an Android phone and reproduces that speed as a physical signal usable by an automotive ECU or speedometer.

## 🔗 Related Project (Android GPS Source)

This firmware is designed to pair with the Android application found here:

👉 **GPStoSPP (Android GPS → Bluetooth SPP)**  
https://github.com/red-peel/GPStoSPP

## 🧠 System Overview

At a high level, the system replaces a mechanical or inductive speed sensor with a GPS-driven digital pipeline:

```
Android Phone (GPS Speed)
        ↓
Bluetooth Classic (SPP)
        ↓
ESP32
        ↓
Open-Collector / VSS Signal
        ↓
ECU / Speedometer / Odometer
```

The phone handles GPS acquisition and filtering.  
The ESP32 focuses exclusively on **timing accuracy, signal generation, and electrical compatibility**.

## 🛠️ What the ESP32 Does

- Accepts incoming speed data over Bluetooth SPP
- Parses speed values (typically MPH)
- Converts speed into a frequency-based output
- Sinks the vehicle’s VSS signal line using an NPN transistor or equivalent.
- Emulates a factory-style VSS waveform

This allows full vehicle speed functionality without:
- A transmission-mounted sensor
- A tone wheel
- Drivetrain modification

## 📡 Why Bluetooth SPP (Not BLE)

Bluetooth SPP is used because it behaves like a **wireless UART**:

- Continuous byte stream
- Deterministic timing
- Minimal protocol overhead
- Simple parsing on embedded hardware

BLE introduces latency, packetization, and complexity that are unnecessary for this use case.

## 🔌 Hardware Expectations

Typical connections:

- **12V switched** → Buck converter → ESP32 VIN / 5V
- **Ground** → Common vehicle ground
- **VSS signal line** → Open-collector output from ESP32 transistor stage

The ESP32 never drives the line high.  
It only **pulls the line low**, matching OEM sensor behavior.

## 🧪 Calibration Notes

Speed accuracy depends on:
- Correct pulses-per-mile configuration
- Stable Bluetooth connection
- Consistent GPS update rate from the phone

Low-speed resolution is intentionally prioritized to preserve drivability and odometer accuracy.

## 📂 Project Structure

```
SPPtoVSS/
├── main/
│   ├── bluetooth/
│   ├── signal/
│   └── app_main.c
├── components/
├── CMakeLists.txt
└── sdkconfig
```

Key areas:
- `bluetooth/` → SPP receive + buffering
- `signal/` → Frequency generation / timing
- `app_main.c` → System orchestration

## 🧩 Intended Use Case

This firmware exists specifically to:
- Emulate a VSS for testing or retrofit scenarios
- Enable GPS-based speed generation
- Support vehicles where physical sensors are impractical

It is **not** a generic Bluetooth project.

## 🧠 Notes

- Designed for ESP-IDF
- Requires Bluetooth Classic support
- Pairs exclusively with **GPStoSPP**. ***If*** you can pitch it a number over SPP serial, it can work.
- If you know why you need this, you’re already ahead of the curve
---



# ESP32 GPS → VSS Emulator Hardware Schematic

## Overview
This module emulates a vehicle speed sensor (VSS) by sinking the ECU’s VSS signal line to ground using an NPN transistor.
The ESP32 generates pulses based on GPS speed data received over Bluetooth SPP.

The ECU provides its own pull-up (~12–15 V).
The ESP32 **never** drives the signal high — it only pulls it low.


## External Connections (Vehicle Side)

| Pin | Signal | Description |
|---:|--------|-------------|
| 1 | +12V Switched | IGN/ACC power source |
| 2 | GND | Vehicle ground (shared) |
| 3 | VSS Signal | ECU VSS pull-up line |


## Power Circuit (12V → 5V)

```
Vehicle +12V (switched)
        |
       [F1] 1A fuse
        |
        +---------------------> LM2596 IN+
        |
      [C1] 10–47µF electrolytic (optional, input filtering)
        |
Vehicle GND -------------------> LM2596 IN-
```

### Buck Converter Output

```
LM2596 OUT+ (5.0V)  -----> +5V rail -----> ESP32 VIN / 5V pin
LM2596 OUT-         -----> GND rail -----> ESP32 GND
```

### Output Stabilization (recommended)

```
+5V rail ----[C2] 220–470µF electrolytic ---- GND
+5V rail ----[C3] 0.1µF ceramic (optional) -- GND
```

## VSS Signal Emulation (Working Configuration)

### Core NPN Sink Stage

```
ESP32 GPIO25 ----[R1 330–470Ω]-----> NPN BASE
                         |
                         +----[R2 100kΩ]----> GND

NPN EMITTER ----------------------> GND (vehicle ground)

ECU VSS SIGNAL -------------------> NPN COLLECTOR
```

**Important notes:**
- The collector connects **directly** to the ECU VSS signal.
- No 1k series resistor is used on the collector.
- The ECU pull-up supplies the HIGH level (~12–15 V).
- The transistor pulls the line LOW (<0.5 V) during pulses.



## Optional Status LED (Safe With ECU Connected)

```
ESP32 GPIO25 ----[R3 330–1kΩ]-----> LED ANODE
LED CATHODE ----------------------> GND
```

This LED indicates pulse activity without touching the ECU signal line.


## Component List

### Core
- ESP32 DevKit (ESP32-WROOM-32 / DevKitV1)
- LM2596 buck converter (12 V → 5 V)
- NPN transistor (2N2222 / PN2222 / BC337)

### Resistors
- R1: 330–470 Ω (base resistor)
- R2: 100 kΩ (base pulldown)
- R3: 330 Ω–1 kΩ (optional LED)

### Capacitors
- C1: 10–47 µF electrolytic ≥16 V (optional, 12 V input)
- C2: 220–470 µF electrolytic ≥10 V (5 V rail)
- C3: 0.1 µF ceramic (optional, 5 V rail)

### Protection / Hardware
- Inline fuse holder + 1 A fuse
- 3-pin automotive connector (Superseal / MX150 / M12)
- Perfboard or PCB
- Enclosure with strain relief

## Electrical Rules (Non-Negotiable)

1. **All grounds must be common**
   Vehicle GND = buck GND = ESP32 GND = transistor emitter.

2. **ECU signal wire must only connect to the transistor collector**
   Never connect ECU voltage to ESP32 pins.

3. **The ESP32 never drives the VSS line high**
   It only sinks current, emulating an open-collector sensor.

---
![20260201_175548](https://github.com/user-attachments/assets/243915f6-eb67-4118-82b3-09ecd5827cb9)
![20260201_180928](https://github.com/user-attachments/assets/83717af4-90b6-4231-b320-5e36444c74fc)
![20260201_180936](https://github.com/user-attachments/assets/49887da1-008a-48d9-9b09-0fd20711ad56)

