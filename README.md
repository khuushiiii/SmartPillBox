# Smart Pill Box — Adaptive Reminder System

An IoT-based pill box that improves medication adherence through dual-sensor confirmation and an adaptive reminder algorithm that learns a user's behavior over time.

![C++](https://img.shields.io/badge/C++-ESP32-blue) ![Firebase](https://img.shields.io/badge/Firebase-Realtime%20DB-orange) ![Cost](https://img.shields.io/badge/Build%20Cost-~₹1000-green)

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [How It Works](#how-it-works)
- [Hardware & Pin Configuration](#hardware--pin-configuration)
- [Running the Project](#running-the-project)
- [Contributors](#contributors)
- [License](#license)

## Overview

Missed medication doses are a common problem, especially for patients managing multiple daily prescriptions. This project builds a low-cost, IoT-based pill box that confirms whether a dose was actually taken — not just whether the box was opened — using a dual-sensor approach, and adapts its reminder timing based on the user's real intake patterns over time. All activity syncs to Firebase for real-time, remote monitoring.

## Features

- Dual confirmation system — an IR sensor detects hand presence, and a load cell confirms the pill was actually removed, avoiding false positives from the box simply being opened
- Adaptive reminder algorithm — learns user behavior and adjusts reminder timing automatically
- Real-time monitoring — Firebase Realtime Database integration with a live dashboard
- Multi-slot management — separate Morning, Afternoon, Evening, and Night compartments
- Low-cost build — implemented for roughly ₹1000 in hardware

## How It Works

1. The system activates for the current time slot
2. An LED begins blinking as a reminder
3. The IR sensor detects hand movement near the box
4. The load cell detects a weight drop, confirming pill removal
5. If both conditions are satisfied, the dose is logged as taken; otherwise it's logged as missed
6. Results are stored in Firebase, and the system updates future reminder timing based on this history

## Hardware & Pin Configuration

HX711 (Load Cell) → ESP32
- VCC → 3.3V
- GND → GND
- DT → GPIO 4
- SCK → GPIO 5

IR Sensor → ESP32
- VCC → 3.3V
- GND → GND
- OUT → GPIO 33

LEDs → ESP32
- Morning → GPIO 14
- Afternoon → GPIO 27
- Evening → GPIO 26
- Night → GPIO 25

## Running the Project

1. Connect the hardware according to the pin configuration above
2. Upload `emb_pro1.ino` to the ESP32
3. Open the Serial Monitor at 115200 baud
4. Calibrate the load cell (empty box, then full box)
5. The system starts monitoring automatically

## Contributors

- [Khushi Sharma](https://github.com/khuushiiii)
- [Vihan Wadhawan](https://github.com/vihanwadhawan)

## License

This project is open source and available under the [MIT License](LICENSE).
