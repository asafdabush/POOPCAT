# 🐈 POOPCAT — Ultrasonic Cat Repeller

> **PoopBlocker 3000** — A humane, IoT-enabled ultrasonic deterrent system that keeps cats away from your garden, without harming them or bothering humans.

---

## 🧠 The Problem

My parents' garden became a regular bathroom spot for neighborhood cats. As an electrical engineer, I was given a clear mission: build a humane, effective, and non-invasive solution.

## 💡 The Approach

Cats hear up to **~60 kHz**, while humans top out at **~20 kHz**. By generating an ultrasonic signal around **40 kHz**, the system creates a highly unpleasant environment for cats — completely inaudible to humans.

**The key challenges:**
- Cover an **8×4 m garden** without false triggers from wind or leaves
- Hide the sensor inside a plastic enclosure (radar waves must pass through, but not concrete walls)
- Enable **remote on/off and scheduling** via smartphone

---

## ⚙️ Hardware Overview

| Component | Role |
|-----------|------|
| **ESP8266 (NodeMCU)** | Main controller + WiFi + Blynk IoT |
| **TC4427 MOSFET Driver** | Differential drive for efficient piezo excitation |
| **Piezo Ultrasonic Transducers** | 40 kHz resonant speakers |
| **HLK-LD2420 Radar Sensor** | Precise motion detection, ignores wind/leaves |
| **Push Button** | Local toggle (on/off) |
| **Status LED** | System state feedback |

---

## 🔌 Pin Mapping

| Pin | GPIO | Function |
|-----|------|----------|
| D1  | GPIO5  | Ultrasonic A (TC4427 IN+) |
| D2  | GPIO4  | Ultrasonic B (TC4427 IN-, inverted) |
| D5  | GPIO14 | Motion input (HLK-LD2420) |
| D6  | GPIO12 | Button input (INPUT_PULLUP) |
| D7  | GPIO13 | Status LED |

---

## 📱 Blynk Virtual Pins

| Pin | Direction | Description |
|-----|-----------|-------------|
| V0 | Write | Remote enable/disable |
| V1 | Write | Frequency slider (1000-35000 Hz) |
| V2 | Read  | Motion status LED |
| V3 | Read  | Speaker active LED |

---

## 📊 Oscilloscope Verification

Measured output on Keysight MSO-X 4024A:

| Parameter | Value |
|-----------|-------|
| Frequency | **41.322 kHz** |
| Amplitude | **10.313 Vpp** |
| Waveform  | Square wave (differential) |

---

## 🚀 Getting Started

### 1. Clone the repo
Clone: https://github.com/asafdabush/POOPCAT.git

### 2. Configure credentials
Copy secrets_template.h to secrets.h and fill in your WiFi SSIDs, passwords, and Blynk token.

### 3. Install dependencies (Arduino IDE)
- ESP8266WiFi (via ESP8266 board package)
- BlynkSimpleEsp8266 (via Library Manager)

### 4. Flash to ESP8266
Select NodeMCU 1.0 (ESP-12E Module), upload, done.

---

## 👤 Author

**Asaf Refael Dabush**
Electrical & Electronics Engineering - Ariel University, 2026

---

## 🪪 License

Released under the MIT License.
