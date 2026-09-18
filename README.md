# Wireless IoT Flex Sensor Glove & Servo Control using ESP32-C3 SuperMini

A secure, wireless IoT project featuring real-time gesture tracking using flex sensors on a glove, transmitting data via **BLE (Bluetooth Low Energy) with MITM Security**, and controlling remote servo motors synchronously[cite: 1, 2, 3, 4].

---

## 🛠️ System Architecture

The project consists of two independent **ESP32-C3 SuperMini** nodes:
1. **Server Node (The Glove):** Reads analog values from 3 flex sensors, applies oversampling and Exponential Moving Average (EMA) filtering, maps them to angles, and acts as a BLE GATT Server[cite: 2, 4].
2. **Client Node (The Receiver):** Scans and connects securely to the Server, receives sensor packets via notifications, and drives 3 micro servo motors accordingly[cite: 1, 3].

---

## 📌 Features

* **BLE Secure Pairing:** Utilizes Secure Connections (SC), MITM protection, and pre-shared static passkey authentication (`123456`) with bonding[cite: 3, 4].
* **Advanced Signal Filtering:** Employs oversampling ($16\times$) and EMA filtering ($\alpha = 0.20$) to ensure smooth, jitter-free analog readings from the flex sensors[cite: 4].
* **Onboard RGB Status Engine:** Implements a 3-mode LED state machine utilizing the built-in NeoPixel to track connection and encryption status[cite: 3, 4].

---

## 🔌 Pinout & Circuit Wiring

### **Server Node (Flex Sensor Glove)**
| Component | ESP32-C3 SuperMini Pin | Description |
| :--- | :--- | :--- |
| **Flex Sensor 1** | `GPIO 0` (ADC0) | Finger 1 Analog Input |
| **Flex Sensor 2** | `GPIO 1` (ADC1) | Finger 2 Analog Input |
| **Flex Sensor 3** | `GPIO 2` (ADC2) | Finger 3 Analog Input |

### **Client Node (Servo Receiver)**
| Component | ESP32-C3 SuperMini Pin | Description |
| :--- | :--- | :--- |
| **Servo Motor 1** | `GPIO 3` | PWM Control Output |
| **Servo Motor 2** | `GPIO 4` | PWM Control Output |
| **Servo Motor 3** | `GPIO 5` | PWM Control Output |

---

Demonstration of Project - (https://youtu.be/QTZwDk5j4d4)

## 🚀 Getting Started

### Prerequisites
* [Arduino IDE](https://www.arduino.cc/) configured for ESP32 boards.
* Required Libraries: `ESP32Servo`

### Flashing Code
1. Upload `bleServer.ino` to the glove's ESP32-C3 SuperMini[cite: 4].
2. Upload `bleClient.ino` to the receiver's ESP32-C3 SuperMini[cite: 3].
3. Power both modules. They will automatically pair, establish an encrypted MITM session, and mirror your glove flex gestures onto the servos[cite: 3, 4].
