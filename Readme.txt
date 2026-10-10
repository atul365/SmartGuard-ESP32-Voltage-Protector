# SmartGuard ESP32 – Voltage and Overcurrent Protector

This project was developed for FSMB Recruitment 2026 Phase 1. It is an ESP32-based voltage and overcurrent protection prototype simulated in Wokwi.

The ESP32 monitors simulated voltage and current values and disconnects the load through a relay when a fault occurs. I also developed a web dashboard to monitor the system and change protection limits.

## Features

- Undervoltage, overvoltage and overcurrent detection
- Relay-based load disconnection
- Red LED fault indication
- Fault latching and manual reset
- Configurable voltage and current limits
- Serial Monitor communication
- MQTT telemetry and settings commands
- Node.js backend and web dashboard

## Components

ESP32 DevKit, two slide potentiometers, relay module, red and green LEDs, pushbutton and resistors.

The potentiometers simulate voltage (0–300 V) and current (0–10 A). No mains voltage is used.

## Default Limits

| Setting | Value |
|---|---|
| Minimum voltage | 200 V |
| Maximum voltage | 250 V |
| Maximum current | 5 A |

## Run the Wokwi Simulation

Project link:

https://wokwi.com/projects/477426343581976577

1. Open the project and start the simulation.
2. Adjust the two potentiometers.
3. Press the physical reset button when both readings are safe.
4. Change voltage or current beyond the limits to test automatic disconnection.
5. Observe the relay, LEDs and Serial Monitor.

## Run the Web Dashboard

Install Node.js and npm first.

Open a terminal inside `src/backend` and run:

```bash
npm install
node server.js
```

Then open `http://localhost:3000` in a browser.

Keep the Wokwi simulation running. The dashboard receives readings through the public HiveMQ MQTT broker and Node.js backend.

## Change Protection Limits

The Serial Monitor supports these commands:

- `STATUS` – Display readings and protection status
- `SET_OV 240` – Set maximum voltage to 240 V
- `SET_UV 190` – Set minimum voltage to 190 V
- `SET_OC 4` – Set maximum current to 4 A
- `RESET` – Request reset if readings are safe
- `HELP` – Show commands

The dashboard also supports changing the three protection limits through its Save Settings button. The updated limits are checked against telemetry received from the ESP32.

The dashboard Reset Fault button is not implemented. Reset is performed using the Wokwi pushbutton or Serial Monitor.

## Testing

Screenshots of normal operation, voltage faults, overcurrent, fault latching and reset behavior are included in `test_evidence/`.

## Limitations

This project is a simulation prototype, not a real electrical safety device. Voltage and current are represented by potentiometers. The public MQTT broker is used only for demonstration, and the remote settings channel is not authenticated. The design is not suitable for controlling real mains-connected appliances without additional hardware, security and safety testing.
