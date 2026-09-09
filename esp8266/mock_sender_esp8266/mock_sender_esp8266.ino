/*
 * MOCK Sender - Dashboard Testing Only (NO LoRa hardware required)
 * ---------------------------------------------------------------------
 * Board: bare ESP8266 (NodeMCU / Wemos D1 Mini) - no LoRa module, no wiring needed
 *
 * Stand-in for lora_sender_wifi.ino for testing the DASHBOARD itself
 * without a LoRa module. Supports BOTH transports so the dashboard can
 * reach this board over WiFi (HTTP) or USB (Serial) - useful as a
 * presentation-day fallback if WiFi misbehaves.
 *
 * WiFi endpoints:   GET /status, GET /log, POST /action
 * Serial protocol:  send "STATUS" | "LOG" | "ACTION:<json>" + newline
 *                    reply is always "RESP:<json>" + newline
 *
 * Fill in WIFI_SSID / WIFI_PASSWORD below before flashing.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* WIFI_SSID = "Laptop";
const char* WIFI_PASSWORD = "12121212";

ESP8266WebServer server(80);

uint32_t packetCounter = 0;
bool autoSendEnabled = true;
bool encryptionEnabled = false;
const unsigned long SEND_INTERVAL_MS = 2000;
unsigned long lastSendTime = 0;

#define LOG_SIZE 15
String logBuffer[LOG_SIZE];
int logIndex = 0;

void addLog(String line) {
  logBuffer[logIndex] = line;
  logIndex = (logIndex + 1) % LOG_SIZE;
  Serial.println(line);
}

String extractJsonString(const String &body, const String &key) {
  String pattern = "\"" + key + "\"";
  int keyPos = body.indexOf(pattern);
  if (keyPos == -1) return "";
  int colon = body.indexOf(':', keyPos);
  int firstQuote = body.indexOf('"', colon + 1);
  int secondQuote = body.indexOf('"', firstQuote + 1);
  if (firstQuote == -1 || secondQuote == -1) return "";
  return body.substring(firstQuote + 1, secondQuote);
}

void sendCors() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void simulateSendPacket() {
  packetCounter++;
  float simulatedTemp = 20.0 + (packetCounter % 15);
  String payload = "SEQ:" + String(packetCounter) + "|TEMP:" + String(simulatedTemp, 1);
  addLog("[SENT-SIM] #" + String(packetCounter) + " -> " + payload + "  (no radio - simulated)");
}

// ---- Shared builders used by BOTH transports ----
String buildStatusJson() {
  String json = "{";
  json += "\"role\":\"sender\",";
  json += "\"uptime_s\":" + String(millis() / 1000) + ",";
  json += "\"packets_sent\":" + String(packetCounter) + ",";
  json += "\"auto_send\":" + String(autoSendEnabled ? "true" : "false") + ",";
  json += "\"encryption\":" + String(encryptionEnabled ? "true" : "false") + ",";
  json += "\"wifi_rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\"";
  json += "}";
  return json;
}

String buildLogJson() {
  String json = "{\"lines\":[";
  for (int i = 0; i < LOG_SIZE; i++) {
    int idx = (logIndex + i) % LOG_SIZE;
    if (logBuffer[idx].length() > 0) {
      json += "\"" + logBuffer[idx] + "\"";
      if (i < LOG_SIZE - 1) json += ",";
    }
  }
  json += "]}";
  return json;
}

String executeAction(const String &body) {
  String cmd = extractJsonString(body, "cmd");

  if (cmd == "send_now") {
    simulateSendPacket();
    return "{\"result\":\"sent\"}";
  } else if (cmd == "send_custom") {
    String payload = extractJsonString(body, "payload");
    if (payload.length() == 0) return "{\"error\":\"empty payload\"}";
    packetCounter++;
    addLog("[SENT-CUSTOM] #" + String(packetCounter) + " -> " + payload + "  (no radio - simulated)");
    return "{\"result\":\"sent\"}";
  } else if (cmd == "start_auto") {
    autoSendEnabled = true;
    addLog("[CTRL] auto-send enabled");
    return "{\"result\":\"auto started\"}";
  } else if (cmd == "stop_auto") {
    autoSendEnabled = false;
    addLog("[CTRL] auto-send disabled");
    return "{\"result\":\"auto stopped\"}";
  } else if (cmd == "terminate") {
    autoSendEnabled = false;
    addLog("[CTRL] *** TERMINATED by operator ***");
    return "{\"result\":\"terminated\"}";
  } else if (cmd == "enable_encryption") {
    encryptionEnabled = true;
    addLog("[CTRL] encryption enabled (placeholder hook)");
    return "{\"result\":\"encryption enabled\"}";
  } else if (cmd == "disable_encryption") {
    encryptionEnabled = false;
    addLog("[CTRL] encryption disabled");
    return "{\"result\":\"encryption disabled\"}";
  }
  return "{\"error\":\"unknown cmd\"}";
}

// ---- WiFi/HTTP transport ----
void handleStatus() { sendCors(); server.send(200, "application/json", buildStatusJson()); }
void handleLog() { sendCors(); server.send(200, "application/json", buildLogJson()); }
void handleAction() {
  sendCors();
  String result = executeAction(server.arg("plain"));
  bool isError = result.indexOf("\"error\"") != -1;
  server.send(isError ? 400 : 200, "application/json", result);
}
void handleOptions() { sendCors(); server.send(204); }

// ---- USB/Serial transport ----
void handleSerialLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  if (line == "STATUS") {
    Serial.println("RESP:" + buildStatusJson());
  } else if (line == "LOG") {
    Serial.println("RESP:" + buildLogJson());
  } else if (line.startsWith("ACTION:")) {
    Serial.println("RESP:" + executeAction(line.substring(7)));
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { }

  Serial.println("=== MOCK Sender (dashboard testing, no LoRa) ===");

  Serial.println("Serial transport ready NOW: send STATUS / LOG / ACTION:{...} lines.");
  Serial.println("(WiFi connects in the background - Serial mode does not wait for it.)");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non-blocking: connects in the background

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleOptions);
  server.begin();

  addLog("[BOOT] mock sender online");
}

void loop() {
  server.handleClient();

  static bool wifiAnnounced = false;
  if (!wifiAnnounced && WiFi.status() == WL_CONNECTED) {
    wifiAnnounced = true;
    Serial.print("WiFi connected. IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("^^^ Enter this IP into the dashboard's Sender field (WiFi mode) ^^^");
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleSerialLine(line);
  }

  if (autoSendEnabled) {
    unsigned long now = millis();
    if (now - lastSendTime >= SEND_INTERVAL_MS) {
      lastSendTime = now;
      simulateSendPacket();
    }
  }
}
