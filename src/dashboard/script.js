console.log("SmartGuard Dashboard Started");

// ================================
// BACKEND API
// ================================

const API_URL = "/api/telemetry";
const SETTINGS_API = "/api/settings";

const UPDATE_INTERVAL = 2000;
const CONFIRM_TIMEOUT = 12000;

// ================================
// HTML ELEMENTS
// ================================

const voltageElement = document.getElementById("voltage");
const currentElement = document.getElementById("current");
const loadElement = document.getElementById("load");

const conditionElement = document.getElementById("condition");
const faultElement = document.getElementById("fault");
const statusElement = document.getElementById("mqtt-status");

const minVoltageInput = document.getElementById("minVoltageInput");
const maxVoltageInput = document.getElementById("maxVoltageInput");
const maxCurrentInput = document.getElementById("maxCurrentInput");

const saveSettingsButton = document.getElementById("saveSettings");
const commandStatus = document.getElementById("commandStatus");

// ================================
// SYSTEM STATE
// ================================

let deviceOnline = false;

let settingsPending = false;
let pendingLimits = null;
let pendingSince = 0;

let lastConfirmedSettings = null;

// ================================
// HELPER FUNCTIONS
// ================================

function setText(element, value) {
  if (element) {
    element.textContent = value;
  }
}

function updateInput(input, value) {
  if (!input) return;

  // Do not overwrite while user is typing.
  if (document.activeElement === input) {
    return;
  }

  // Do not overwrite while waiting for confirmation.
  if (settingsPending) {
    return;
  }

  input.value = value;
}

function clearReadings(status) {
  setText(statusElement, status);

  setText(voltageElement, "-- V");
  setText(currentElement, "-- A");
  setText(loadElement, "UNKNOWN");

  setText(conditionElement, "UNKNOWN");
  setText(faultElement, "UNKNOWN");

  conditionElement.className = "";
  faultElement.className = "";
}

function updateButtonState() {
  saveSettingsButton.disabled = !deviceOnline || settingsPending;
}

function sameNumber(a, b) {
  return Math.abs(Number(a) - Number(b)) < 0.01;
}

// ================================
// FETCH TELEMETRY
// ================================

async function updateDashboard() {
  try {
    const response = await fetch(API_URL, {
      cache: "no-store",
    });

    if (!response.ok) {
      throw new Error("Backend request failed");
    }

    const data = await response.json();

    // Device freshness check
    if (data.device !== "ONLINE") {
      deviceOnline = false;

      clearReadings("Device: " + (data.device || "OFFLINE"));

      updateButtonState();
      return;
    }

    deviceOnline = true;

    setText(statusElement, "Device: ONLINE");

    // ================================
    // LIVE MEASUREMENTS
    // ================================

    setText(voltageElement, Number(data.voltage).toFixed(2) + " V");

    setText(currentElement, Number(data.current).toFixed(2) + " A");

    setText(loadElement, data.load);

    // ================================
    // PROTECTION STATUS
    // ================================

    setText(conditionElement, data.condition);
    setText(faultElement, data.fault);

    conditionElement.className =
      data.condition === "NORMAL" ? "safe-text" : "fault-text";

    faultElement.className = data.fault === "NONE" ? "safe-text" : "fault-text";

    // ================================
    // PROTECTION SETTINGS
    // ================================

    const actualLimits = {
      minVoltage: Number(data.minVoltage),
      maxVoltage: Number(data.maxVoltage),
      maxCurrent: Number(data.maxCurrent),
    };

    if (settingsPending && pendingLimits) {
      const confirmed =
        sameNumber(actualLimits.minVoltage, pendingLimits.minVoltage) &&
        sameNumber(actualLimits.maxVoltage, pendingLimits.maxVoltage) &&
        sameNumber(actualLimits.maxCurrent, pendingLimits.maxCurrent);

      if (confirmed) {
        settingsPending = false;
        pendingLimits = null;
        pendingSince = 0;

        setText(commandStatus, "ESP32 settings updated successfully!");

        console.log("Settings confirmed by ESP32");
      } else if (Date.now() - pendingSince > CONFIRM_TIMEOUT) {
        settingsPending = false;
        pendingLimits = null;
        pendingSince = 0;

        setText(
          commandStatus,
          "Settings not confirmed. Check Wokwi Serial Monitor.",
        );
      }
    }

    lastConfirmedSettings = actualLimits;

    // Sync actual ESP32 limits to inputs
    updateInput(minVoltageInput, actualLimits.minVoltage);

    updateInput(maxVoltageInput, actualLimits.maxVoltage);

    updateInput(maxCurrentInput, actualLimits.maxCurrent);

    updateButtonState();

    console.log("ESP32 Telemetry:", data);
  } catch (error) {
    console.error("Backend Error:", error.message);

    deviceOnline = false;

    clearReadings("Backend: Disconnected");

    updateButtonState();
  }
}

// ================================
// SAVE SETTINGS
// ================================

saveSettingsButton.addEventListener("click", async () => {
  if (!deviceOnline) {
    setText(commandStatus, "Device is offline. Cannot change settings.");
    return;
  }

  if (settingsPending) {
    return;
  }

  // Read input values as strings first
  const minText = minVoltageInput.value.trim();
  const maxText = maxVoltageInput.value.trim();
  const currentText = maxCurrentInput.value.trim();

  if (!minText || !maxText || !currentText) {
    setText(commandStatus, "Please fill all settings.");
    return;
  }

  const minVoltage = Number(minText);
  const maxVoltage = Number(maxText);
  const maxCurrent = Number(currentText);

  // Validate before sending
  const valid =
    Number.isFinite(minVoltage) &&
    Number.isFinite(maxVoltage) &&
    Number.isFinite(maxCurrent) &&
    minVoltage >= 0 &&
    maxVoltage <= 300 &&
    minVoltage + 1 < maxVoltage &&
    maxCurrent > 0 &&
    maxCurrent <= 10;

  if (!valid) {
    setText(commandStatus, "Invalid limits. Check voltage and current ranges.");
    return;
  }

  const requestedLimits = {
    minVoltage,
    maxVoltage,
    maxCurrent,
  };

  settingsPending = true;
  pendingLimits = requestedLimits;
  pendingSince = Date.now();

  updateButtonState();

  setText(commandStatus, "Sending settings to ESP32...");

  try {
    const response = await fetch(SETTINGS_API, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
      },
      body: JSON.stringify(requestedLimits),
    });

    const result = await response.json();

    if (!response.ok) {
      throw new Error(result.message || "Settings request failed");
    }

    setText(
      commandStatus,
      "Commands published. Waiting for ESP32 confirmation...",
    );

    // Check again immediately
    await updateDashboard();
  } catch (error) {
    settingsPending = false;
    pendingLimits = null;
    pendingSince = 0;

    setText(commandStatus, error.message);

    updateButtonState();
  }
});

// ================================
// START DASHBOARD
// ================================

updateButtonState();

// Load immediately
updateDashboard();

// Refresh every 2 seconds
setInterval(updateDashboard, UPDATE_INTERVAL);
