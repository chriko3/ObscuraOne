# 📸 Obscura One – Open Source Camera

<p align="center">
  <img src="img/camera.jpg" width="48%" />
  <img src="img/camera2.jpg" width="48%" />
</p>

A minimalist, fully open-source camera project with a nostalgic 2000s vibe.
Designed to be simple, hackable, and entirely 3D printable.

---

## ⚙️ Key Facts

* 🧠 **Difficulty Level:** 3 / 5
* ⏱️ **Build Time:** ~4 hours
* 🧩 **Fully Open Source**
* 🖨️ **100% 3D Printable Housing**
* 💸 **Estimated Cost:** under €30
* 🛒 **Parts Availability:** Amazon / AliExpress

---

## 🛠️ Built With

* **3D Design:** FreeCAD
* **Firmware:** Arduino
* **AI Assistance for Code and Read me:** Cursor AI & Chat GPT

---

## 🎮 Features

* 🎛️ **Single-button control system**
* 📷 **Photo capture modes:**

  * 1 click → Normal photo
  * 2 clicks → Photo with flash
* 📡 **Wi-Fi mode:**

  * Hold for 3 seconds → Activate Wi-Fi for photo download
* 📳 **Haptic feedback via vibration module**

---

## 🧰 Components

* AI Thinker ESP32-CAM Module
  [https://amzn.eu/d/024SX1PO](https://amzn.eu/d/024SX1PO)
* SD Card
  [https://amzn.eu/d/09k1OFlh](https://amzn.eu/d/09k1OFlh)
* TC4056 USB Charging Module
  [https://amzn.eu/d/02Zvqhaq](https://amzn.eu/d/02Zvqhaq)
* 1100 mAh LiPo Battery
  [https://amzn.eu/d/0aaQkbQR](https://amzn.eu/d/0aaQkbQR)
* Mini Push Button Switch (self-locking)
  [https://amzn.eu/d/0bQ7lpwj](https://amzn.eu/d/0bQ7lpwj)
* Step-Up Power Module
  [https://amzn.eu/d/0cnFAXdy](https://amzn.eu/d/0cnFAXdy)
* PWM Vibration Motor Module
  [https://amzn.eu/d/0cxGOyEb](https://amzn.eu/d/0cxGOyEb)
* Tactile Button
  [https://amzn.eu/d/07s1kupT](https://amzn.eu/d/07s1kupT)
* Optional: Hand strap
  [https://amzn.eu/d/002JGn6S](https://amzn.eu/d/002JGn6S)

*Note: These are not affiliate links.*

---

## 🖨️ 3D Printing

All structural parts are designed to be fully 3D printable.

**Recommended settings:**

* Material: PETG
* Layer height: 0.15–0.2 mm
* Infill: ~20%
* Supports: Required for case and back cover

* 3D model on Printables: [https://www.printables.com/model/1662215-obscura-one](https://www.printables.com/model/1662215-obscura-one)

---

## 🔌 Assembly

1. Print all required parts
2. Format SD Card to FAT32 and insert it
3. Assemble the electronics according to the wiring diagram
4. Insert all modules into the housing
5. Secure the button, vibration motor, charging module, and step-up module using hot glue
6. Close the case using super glue
7. Flash the firmware

<p align="center">
  <img src="img/components.png" width="48%" />
    <img src="img/3dparts.png" width="48%" />
</p>

---

## 📝 Arduino Setup

1. Open Arduino IDE and add the ESP32 board manager URL:
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Go to **Tools → Board → Board Manager**, search for **ESP32** and install it.
3. Select **AI Thinker ESP32-CAM** as the board.
4. Install any missing libraries via Arduino Library Manager (Sketch → Include Library → Manage Libraries).

---

## 🔌 Circuit

**Pin Mapping:**

* TC4056 OUT+ → Mini Push Button Switch → Step-Up VIN
* TC4056 OUT− → Step-Up GND
* Battery + → TC4056 B+
* Battery − → TC4056 B−
* Step-Up VOUT → 5V ESP32-CAM Module
* Step-Up GND → ESP32-CAM GND
* Tactile Button → ESP32 GND
* Tactile Button → GPIO 13
* PWM Vibration Motor Module IN → GPIO 12
* PWM Vibration Motor Module VCC → 5V ESP32
* PWM Vibration Motor Module GND → ESP32 GND

**Wiring Diagram:**
<p align="center">
  <img src="img/wiringdiagram.png" width="80%"/>
</p>

---

## 🖼️ Example Photos

Here are some sample shots taken with Obscura One:

<p align="center">
  <img src="img/img_1.jpg" width="48%" />
  <img src="img/img_2.jpg" width="48%" />
  <img src="img/img_3.jpg" width="48%" />
</p>

---

## 📜 License

This project is fully open source.
MIT License

---
