const express = require("express");
const mqtt = require("mqtt");
const path = require("path");

const app = express();
const PORT = 3000;

// =====================================
// EXPRESS CONFIGURATION
// =====================================

app.use(express.json());

// Serve dashboard files
app.use(express.static(path.join(__dirname, "..")));

// =====================================
// MQTT CONFIGURATION
// =====================================

const MQTT_BROKER = "mqtt://broker.hivemq.com:1883";

const MQTT_TOPIC = "smartguard/fsmb2026/demo7391/telemetry";

const MQTT_COMMAND_TOPIC = "smartguard/fsmb2026/demo7391/command";

let latestTelemetry = null;
let lastReceivedAt = null;

// Connect to MQTT broker
const mqttClient = mqtt.connect(MQTT_BROKER, {
  clientId: "smartguard-backend-" + Math.random().toString(16).slice(2),

  reconnectPeriod: 3000,
  connectTimeout: 10000,
});

// =====================================
// MQTT CONNECTION
// =====================================

mqttClient.on("connect", () => {
  console.log("MQTT Broker Connected!");

  mqttClient.subscribe(MQTT_TOPIC, (error) => {
    if (error) {
      console.error("Subscription failed:", error.message);
    } else {
      console.log("Subscribed to:", MQTT_TOPIC);
    }
  });
});

// =====================================
// RECEIVE ESP32 TELEMETRY
// =====================================

mqttClient.on("message", (topic, message) => {
  if (topic !== MQTT_TOPIC) {
    return;
  }

  try {
    const data = JSON.parse(message.toString());

    latestTelemetry = data;
    lastReceivedAt = Date.now();

    console.log("ESP32 Data:", data);
  } catch (error) {
    console.error("Invalid MQTT data:", error.message);
  }
});

mqttClient.on("error", (error) => {
  console.error("MQTT Error:", error.message);
});

// =====================================
// SERVER STATUS API
// =====================================

app.get("/api/status", (req, res) => {
  res.json({
    project: "SmartGuard ESP32",
    server: "ONLINE",
    mqtt: mqttClient.connected ? "CONNECTED" : "DISCONNECTED",
  });
});

// =====================================
// LIVE TELEMETRY API
// =====================================

app.get("/api/telemetry", (req, res) => {
  if (!latestTelemetry) {
    return res.json({
      device: "WAITING",
      message: "No ESP32 data received yet",
    });
  }

  const ageMs = Date.now() - lastReceivedAt;

  res.json({
    ...latestTelemetry,
    device: ageMs < 10000 ? "ONLINE" : "OFFLINE",
    lastReceivedAt: new Date(lastReceivedAt).toISOString(),
    ageMs: ageMs,
  });
});

// =====================================
// CHANGE PROTECTION SETTINGS
// WOKWI SIMULATION ONLY
// =====================================

app.post("/api/settings", async (req, res) => {
  const body = req.body || {};

  const values = [body.minVoltage, body.maxVoltage, body.maxCurrent];

  // Reject missing, empty, or nonnumeric values
  if (
    values.some(
      (value) =>
        value === undefined || value === null || String(value).trim() === "",
    )
  ) {
    return res.status(400).json({
      success: false,
      message: "All three settings are required",
    });
  }

  const minV = Number(body.minVoltage);
  const maxV = Number(body.maxVoltage);
  const maxI = Number(body.maxCurrent);

  if (
    !Number.isFinite(minV) ||
    !Number.isFinite(maxV) ||
    !Number.isFinite(maxI) ||
    minV < 0 ||
    maxV > 300 ||
    minV + 1 >= maxV ||
    maxI <= 0 ||
    maxI > 10
  ) {
    return res.status(400).json({
      success: false,
      message: "Invalid protection limits",
    });
  }

  // Do not send commands if device is offline
  if (!latestTelemetry || Date.now() - lastReceivedAt >= 10000) {
    return res.status(503).json({
      success: false,
      message: "ESP32 is offline",
    });
  }

  if (!mqttClient.connected) {
    return res.status(503).json({
      success: false,
      message: "MQTT broker disconnected",
    });
  }

  // Existing ESP32 handleCommand() accepts these
  const commands = [`SET_OV ${maxV}`, `SET_UV ${minV}`, `SET_OC ${maxI}`];

  try {
    for (const command of commands) {
      await new Promise((resolve, reject) => {
        mqttClient.publish(
          MQTT_COMMAND_TOPIC,
          command,
          {
            qos: 0,
            retain: false,
          },
          (error) => {
            if (error) reject(error);
            else resolve();
          },
        );
      });

      console.log("MQTT Command Published:", command);
    }

    return res.json({
      success: true,
      message: "Commands sent; waiting for ESP32 confirmation",
    });
  } catch (error) {
    return res.status(500).json({
      success: false,
      message: error.message,
    });
  }
});

// =====================================
// START SERVER
// =====================================

app.post("/api/reset", (req, res) => {
  if (!latestTelemetry || Date.now() - lastReceivedAt > 10000) {
    return res.status(503).json({
      success: false,
      message: "ESP32 is offline",
    });
  }

  if (latestTelemetry.condition !== "NORMAL") {
    return res.status(409).json({
      success: false,
      message: "Reset denied: unsafe voltage or current",
    });
  }

  return res.status(403).json({
    success: false,
    message: "Remote reset requires authenticated MQTT",
  });
});

app.listen(PORT, "127.0.0.1", () => {
  console.log(`SmartGuard Server running at http://localhost:${PORT}`);
});
