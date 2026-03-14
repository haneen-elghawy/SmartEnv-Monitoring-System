# SmartEnv-Monitoring-System


## Smart Environment Monitoring & Control System

This folder contains all assignment artefacts for the SWAPD 453 **Smart Environment Monitoring & Control System** built with **ESP32 (Wokwi)**, **MQTT (Mosquitto)** and **Node-RED dashboard**.

- `esp32_code/smart_env.ino` — full ESP32 firmware (sensors, actuators, WiFi + MQTT, rules).
- `esp32_code/libraries.txt` — Arduino / Wokwi library list.
- `wokwi/diagram.json` — Wokwi wiring diagram for ESP32 + sensors + actuators.
- `wokwi/wokwi.toml` — Wokwi network forwarding to local MQTT broker.
- `nodered_flow/flows.json` — Node-RED flow to import (dashboard, control, alerts, logging, status).
- `docs/report_outline.md` — structured outline you can turn into `report.pdf`.

See the sections below for quick‑start instructions.

### 1. Prerequisites

- Node.js (v18+) and Node-RED (`npm install -g node-red`).
- `node-red-dashboard` installed in your user `.node-red` folder.
- Mosquitto broker running on `localhost:1883`.
- Wokwi account (free) on `https://wokwi.com`.

### 2. Running the System (High Level)

1. Start Mosquitto locally on port 1883.
2. Start Node-RED and import `nodered_flow/flows.json`, then open `http://localhost:1880/ui`.
3. Create a new Wokwi ESP32 project, copy `wokwi/diagram.json`, `wokwi/wokwi.toml` and `esp32_code/smart_env.ino` into it.
4. Install the libraries from `esp32_code/libraries.txt` in Wokwi / Arduino.
5. Run the Wokwi simulation; you should see MQTT data reaching Node-RED and the dashboard controlling actuators.

