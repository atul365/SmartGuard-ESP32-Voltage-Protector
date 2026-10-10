
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <WiFi.h>
#include <PubSubClient.h>

// =====================================
// PIN CONFIGURATION
// =====================================

const int VOLTAGE_PIN = 33;
const int CURRENT_PIN = 32;
const int RELAY_PIN = 23;
const int RED_LED_PIN = 19;
const int RESET_BUTTON = 18;

// Based on the tested Wokwi relay behavior
const int RELAY_ON = HIGH;
const int RELAY_OFF = LOW;

// =====================================
// WIFI AND MQTT CONFIGURATION
// =====================================

const char* WIFI_SSID = "Wokwi-GUEST";

const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

const char* MQTT_TOPIC =
  "smartguard/fsmb2026/demo7391/telemetry";

const char* MQTT_COMMAND_TOPIC =
  "smartguard/fsmb2026/demo7391/command";

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

unsigned long lastMqttPublish = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMqttAttempt = 0;

const unsigned long MQTT_INTERVAL = 2000;

// =====================================
// PROTECTION LIMITS
// =====================================

float minVoltage = 200.0f;
float maxVoltage = 250.0f;
float maxCurrent = 5.0f;

// =====================================
// SYSTEM VARIABLES
// =====================================

bool faultLatched = true;
String faultReason = "STARTUP_LOCK";

float voltage = 0.0f;
float current = 0.0f;

unsigned long lastSample = 0;
unsigned long lastPrint = 0;

const unsigned long SAMPLE_INTERVAL = 100;
const unsigned long PRINT_INTERVAL = 2000;

// =====================================
// RESET BUTTON DEBOUNCE
// =====================================

bool lastRawButton = HIGH;
bool stableButton = HIGH;

unsigned long lastButtonChange = 0;

const unsigned long DEBOUNCE_MS = 50;

// Serial command buffer
String inputBuffer = "";

// =====================================
// RELAY AND LED CONTROL
// =====================================

void updateOutputs() {

  if (faultLatched) {

    digitalWrite(RELAY_PIN, RELAY_OFF);
    digitalWrite(RED_LED_PIN, HIGH);

  } else {

    digitalWrite(RELAY_PIN, RELAY_ON);
    digitalWrite(RED_LED_PIN, LOW);
  }
}

// =====================================
// SENSOR MONITORING
// =====================================

void readSensors() {

  int voltageADC = analogRead(VOLTAGE_PIN);
  int currentADC = analogRead(CURRENT_PIN);

  // Simulation-only conversion
  voltage = voltageADC * 300.0f / 4095.0f;
  current = currentADC * 10.0f / 4095.0f;
}

// =====================================
// FAULT DETECTION
// =====================================

String detectFault() {

  if (!isfinite(voltage) || !isfinite(current)) {
    return "SENSOR_ERROR";
  }

  if (voltage < minVoltage) {
    return "UNDERVOLTAGE";
  }

  if (voltage > maxVoltage) {
    return "OVERVOLTAGE";
  }

  if (current > maxCurrent) {
    return "OVERCURRENT";
  }

  return "";
}

// =====================================
// FAULT LATCHING
// =====================================

void checkProtection() {

  String problem = detectFault();

  if (problem.length() == 0) {
    return;
  }

  if (!faultLatched || faultReason == "STARTUP_LOCK") {

    faultLatched = true;
    faultReason = problem;

    updateOutputs();

    Serial.print("FAULT DETECTED: ");
    Serial.println(faultReason);

    Serial.println("RELAY OFF - LOAD DISCONNECTED");
  }
}

// =====================================
// RESET PROTECTION
// =====================================

void resetProtection() {

  readSensors();

  String problem = detectFault();

  if (problem.length() > 0) {

    Serial.print("RESET DENIED: ");
    Serial.println(problem);

    return;
  }

  if (!faultLatched) {

    Serial.println("LOAD ALREADY ON");
    return;
  }

  faultLatched = false;
  faultReason = "NONE";

  updateOutputs();

  Serial.println("RESET SUCCESS");
  Serial.println("RELAY ON - LOAD CONNECTED");
}

// =====================================
// PRINT SYSTEM STATUS
// =====================================

void printStatus() {

  Serial.println("-------------------------");

  Serial.print("Voltage: ");
  Serial.print(voltage, 2);
  Serial.println(" V");

  Serial.print("Current: ");
  Serial.print(current, 2);
  Serial.println(" A");

  Serial.print("Load: ");
  Serial.println(faultLatched ? "OFF" : "ON");

  String currentFault = detectFault();

  Serial.print("Current Condition: ");

  if (currentFault.length() == 0) {
    Serial.println("NORMAL");
  } else {
    Serial.println(currentFault);
  }

  Serial.print("Latched Fault: ");
  Serial.println(faultReason);

  Serial.print("Voltage limits: ");
  Serial.print(minVoltage, 2);
  Serial.print(" - ");
  Serial.println(maxVoltage, 2);

  Serial.print("Current limit: ");
  Serial.println(maxCurrent, 2);

  Serial.println("-------------------------");
}

// =====================================
// VALID NUMERIC INPUT
// =====================================

bool parseNumber(String text, float &result) {

  text.trim();

  if (text.length() == 0 || text.length() > 24) {
    return false;
  }

  char* endPtr;

  result = strtof(text.c_str(), &endPtr);

  return endPtr != text.c_str()
      && *endPtr == '\0'
      && isfinite(result);
}

// =====================================
// SERIAL AND MQTT COMMAND PROCESSING
// =====================================

void handleCommand(String cmd) {

  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "STATUS") {

    readSensors();
    checkProtection();
    printStatus();

    return;
  }

  if (cmd == "RESET") {

    resetProtection();
    return;
  }

  if (cmd == "HELP") {

    Serial.println("AVAILABLE COMMANDS:");
    Serial.println("STATUS");
    Serial.println("RESET");
    Serial.println("SET_OV 250");
    Serial.println("SET_UV 200");
    Serial.println("SET_OC 5");
    Serial.println("HELP");

    return;
  }

  float value;
  bool updated = false;

  // Maximum Voltage
  if (cmd.startsWith("SET_OV ")) {

    if (parseNumber(cmd.substring(7), value)
        && value > minVoltage + 1
        && value <= 300) {

      maxVoltage = value;
      updated = true;

      Serial.println("OV LIMIT UPDATED");
    }
  }

  // Minimum Voltage
  else if (cmd.startsWith("SET_UV ")) {

    if (parseNumber(cmd.substring(7), value)
        && value >= 0
        && value < maxVoltage - 1) {

      minVoltage = value;
      updated = true;

      Serial.println("UV LIMIT UPDATED");
    }
  }

  // Maximum Current
  else if (cmd.startsWith("SET_OC ")) {

    if (parseNumber(cmd.substring(7), value)
        && value > 0
        && value <= 10) {

      maxCurrent = value;
      updated = true;

      Serial.println("OC LIMIT UPDATED");
    }
  }

  else {

    Serial.println("UNKNOWN COMMAND");
    return;
  }

  if (!updated) {

    Serial.println("INVALID SETTING");
    return;
  }

  // Recheck immediately after changing a limit
  readSensors();
  checkProtection();
  printStatus();
}

// =====================================
// PHYSICAL RESET BUTTON
// =====================================

void checkResetButton() {

  bool rawState = digitalRead(RESET_BUTTON);

  unsigned long now = millis();

  if (rawState != lastRawButton) {
    lastButtonChange = now;
  }

  if ((now - lastButtonChange) >= DEBOUNCE_MS) {

    if (rawState != stableButton) {

      stableButton = rawState;

      if (stableButton == LOW) {

        Serial.println("DEBUG: RESET BUTTON PRESSED");
        resetProtection();
      }
    }
  }

  lastRawButton = rawState;
}

// =====================================
// MQTT COMMAND CALLBACK
// =====================================

void mqttCallback(char* topic,
                  byte* payload,
                  unsigned int length) {

  if (strcmp(topic, MQTT_COMMAND_TOPIC) != 0) {
    return;
  }

  if (length == 0 || length > 48) {

    Serial.println("INVALID MQTT COMMAND");
    return;
  }

  String command = "";

  for (unsigned int i = 0; i < length; i++) {
    command += (char)payload[i];
  }

  command.trim();

  Serial.print("MQTT COMMAND RECEIVED: ");
  Serial.println(command);

  // Only allow settings commands over MQTT.
  // Reset remains local / Serial-only.
  if (
    command.startsWith("SET_OV ") ||
    command.startsWith("SET_UV ") ||
    command.startsWith("SET_OC ")
  ) {

    handleCommand(command);

  } else {

    Serial.println("MQTT COMMAND NOT ALLOWED");
  }
}

// =====================================
// WIFI CONNECTION
// =====================================

void setupWiFi() {

  WiFi.mode(WIFI_STA);

  WiFi.begin(WIFI_SSID, "", 6);

  Serial.println("WiFi connection started");
}

// =====================================
// WIFI AND MQTT RECONNECTION
// =====================================

void maintainNetwork() {

  unsigned long now = millis();

  // Check Wi-Fi connection
  if (WiFi.status() != WL_CONNECTED) {

    if (now - lastWiFiAttempt >= 5000) {

      lastWiFiAttempt = now;
      WiFi.reconnect();
    }

    return;
  }

  // Check MQTT connection
  if (!mqtt.connected()) {

    if (now - lastMqttAttempt >= 5000) {

      lastMqttAttempt = now;

      String clientId =
        "smartguard-" +
        String((uint32_t)ESP.getEfuseMac(), HEX);

      if (mqtt.connect(clientId.c_str())) {

        Serial.println("MQTT CONNECTED");

        // Subscribe to settings commands
        if (mqtt.subscribe(MQTT_COMMAND_TOPIC)) {

          Serial.println("MQTT COMMAND SUBSCRIBED");

        } else {

          Serial.println("MQTT SUBSCRIPTION FAILED");
        }

      } else {

        Serial.print("MQTT ERROR: ");
        Serial.println(mqtt.state());
      }
    }

    return;
  }

  // Process incoming MQTT commands
  mqtt.loop();
}

// =====================================
// PUBLISH MQTT TELEMETRY
// =====================================

void publishTelemetry() {

  if (!mqtt.connected()) {
    return;
  }

  String condition = detectFault();

  if (condition.length() == 0) {
    condition = "NORMAL";
  }

  char payload[350];

  snprintf(
    payload,
    sizeof(payload),

    "{"
      "\"voltage\":%.2f,"
      "\"current\":%.2f,"
      "\"load\":\"%s\","
      "\"condition\":\"%s\","
      "\"fault\":\"%s\","
      "\"minVoltage\":%.2f,"
      "\"maxVoltage\":%.2f,"
      "\"maxCurrent\":%.2f"
    "}",

    voltage,
    current,
    faultLatched ? "OFF" : "ON",
    condition.c_str(),
    faultReason.c_str(),
    minVoltage,
    maxVoltage,
    maxCurrent
  );

  bool success = mqtt.publish(
    MQTT_TOPIC,
    payload
  );

  if (success) {

    Serial.println("MQTT TELEMETRY SENT");

  } else {

    Serial.println("MQTT PUBLISH FAILED");
  }
}

// =====================================
// SETUP
// =====================================

void setup() {

  // Keep relay OFF at startup
  digitalWrite(RELAY_PIN, RELAY_OFF);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);

  pinMode(RESET_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);

  analogReadResolution(12);

  faultLatched = true;
  faultReason = "STARTUP_LOCK";

  updateOutputs();

  readSensors();
  checkProtection();

  Serial.println();
  Serial.println("SMARTGUARD ESP32");
  Serial.println("PROTECTION SYSTEM STARTED");
  Serial.println("STARTUP: LOAD OFF");

  Serial.println("Type HELP for commands");

  printStatus();

  // Configure MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  setupWiFi();
}

// =====================================
// MAIN LOOP
// =====================================

void loop() {

  unsigned long now = millis();

  // 1. Monitor sensors
  if (now - lastSample >= SAMPLE_INTERVAL) {

    lastSample = now;

    readSensors();
    checkProtection();
  }

  // 2. Maintain network
  maintainNetwork();

  // 3. Publish telemetry every 2 seconds
  if (millis() - lastMqttPublish >= MQTT_INTERVAL) {

    lastMqttPublish = millis();

    publishTelemetry();
  }

  // 4. Serial command processing
  while (Serial.available() > 0) {

    char c = Serial.read();

    if (c == '\n' || c == '\r') {

      if (inputBuffer.length() > 0) {

        handleCommand(inputBuffer);
        inputBuffer = "";
      }

    } else if (inputBuffer.length() < 48) {

      inputBuffer += c;
    }
  }

  // 5. Physical reset
  checkResetButton();

  // 6. Print status periodically
  if (millis() - lastPrint >= PRINT_INTERVAL) {

    lastPrint = millis();

    printStatus();
  }
}
