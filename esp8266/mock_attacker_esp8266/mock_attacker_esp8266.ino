/*
 * MOCK Attacker - Dashboard Testing Only (NO LoRa hardware required)
 * ------------------------------------------------------------------------
 * Board: bare ESP8266 (NodeMCU / Wemos D1 Mini) - no LoRa module, no wiring needed
 *
 * Stand-in for lora_attacker_wifi.ino. Simulates capturing a packet every
 * few seconds. Supports BOTH transports (WiFi HTTP and USB Serial) - see
 * mock_sender.ino header for the Serial protocol description.
 *
 * Fill in WIFI_SSID / WIFI_PASSWORD below before flashing.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

ESP8266WebServer server(80);

uint32_t capturedCount = 0;
String lastCaptured = "";
bool eavesdropEnabled = true;

const unsigned long SIM_CAPTURE_INTERVAL_MS = 3700;
unsigned long lastSimTime = 0;

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

void simulateCapture() {
  capturedCount++;
  float simulatedTemp = 20.0 + (capturedCount % 15);
  lastCaptured = "SEQ:" + String(capturedCount) + "|TEMP:" + String(simulatedTemp, 1);
  if (eavesdropEnabled) {
    int simRssi = -55 - random(0, 20);
    addLog("[CAPTURED-SIM] " + lastCaptured + " (RSSI " + String(simRssi) + ")");
  }
}

String buildStatusJson() {
  String json = "{";
  json += "\"role\":\"attacker\",";
  json += "\"uptime_s\":" + String(millis() / 1000) + ",";
  json += "\"captured_count\":" + String(capturedCount) + ",";
  json += "\"eavesdrop_enabled\":" + String(eavesdropEnabled ? "true" : "false") + ",";
  json += "\"last_captured\":\"" + lastCaptured + "\",";
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

  if (cmd == "start_eavesdrop") {
    eavesdropEnabled = true;
    addLog("[CTRL] eavesdrop logging enabled");
    return "{\"result\":\"eavesdrop started\"}";
  } else if (cmd == "stop_eavesdrop") {
    eavesdropEnabled = false;
    addLog("[CTRL] eavesdrop logging disabled");
    return "{\"result\":\"eavesdrop stopped\"}";
  } else if (cmd == "terminate") {
    eavesdropEnabled = false;
    addLog("[CTRL] *** TERMINATED by operator ***");
    return "{\"result\":\"terminated\"}";
  } else if (cmd == "replay_last") {
    if (lastCaptured.length() == 0) return "{\"error\":\"nothing captured yet\"}";
    addLog("[REPLAY-SIM] would retransmit -> " + lastCaptured + "  (no radio - simulated)");
    return "{\"result\":\"replayed\"}";
  } else if (cmd == "inject_custom") {
    String payload = extractJsonString(body, "payload");
    if (payload.length() == 0) return "{\"error\":\"empty payload\"}";
    addLog("[INJECT-SIM] would forge -> " + payload + "  (no radio - simulated)");
    return "{\"result\":\"injected\"}";
  }
  return "{\"error\":\"unknown cmd\"}";
}

void handleStatus() { sendCors(); server.send(200, "application/json", buildStatusJson()); }
void handleLog() { sendCors(); server.send(200, "application/json", buildLogJson()); }
void handleAction() {
  sendCors();
  String result = executeAction(server.arg("plain"));
  bool isError = result.indexOf("\"error\"") != -1;
  server.send(isError ? 400 : 200, "application/json", result);
}
void handleOptions() { sendCors(); server.send(204); }

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

  Serial.println("=== MOCK Attacker (dashboard testing, no LoRa) ===");
  randomSeed(analogRead(0));

  Serial.println("Serial transport ready NOW: send STATUS / LOG / ACTION:{...} lines.");
  Serial.println("(WiFi connects in the background - Serial mode does not wait for it.)");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non-blocking: connects in the background

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleOptions);
  server.begin();

  addLog("[BOOT] mock attacker online");
}

void loop() {
  server.handleClient();

  static bool wifiAnnounced = false;
  if (!wifiAnnounced && WiFi.status() == WL_CONNECTED) {
    wifiAnnounced = true;
    Serial.print("WiFi connected. IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("^^^ Enter this IP into the dashboard's Attacker field (WiFi mode) ^^^");
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleSerialLine(line);
  }

  unsigned long now = millis();
  if (now - lastSimTime >= SIM_CAPTURE_INTERVAL_MS) {
    lastSimTime = now;
    simulateCapture();
  }
}
