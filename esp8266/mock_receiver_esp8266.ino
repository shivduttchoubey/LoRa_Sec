/*
 * MOCK Receiver - Dashboard Testing Only (NO LoRa hardware required)
 * -----------------------------------------------------------------------
 * Board: bare ESP8266 (NodeMCU / Wemos D1 Mini) - no LoRa module, no wiring needed
 *
 * Stand-in for lora_receiver_wifi.ino. Simulates a packet "arriving" on
 * its own timer. Supports BOTH transports (WiFi HTTP and USB Serial) -
 * see mock_sender.ino header for the Serial protocol description.
 *
 * Fill in WIFI_SSID / WIFI_PASSWORD below before flashing.
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

ESP8266WebServer server(80);

uint32_t packetsReceived = 0;
uint32_t lastSeenSeq = 0;
uint32_t gapCount = 0;
int lastRssi = -60;
float lastSnr = 8.0;
bool encryptionEnabled = false;

const unsigned long SIM_RECEIVE_INTERVAL_MS = 2300;
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

void simulateReceive() {
  packetsReceived++;
  lastSeenSeq++;
  lastRssi = -50 - (random(0, 25));
  lastSnr = 5.0 + (random(0, 60) / 10.0);

  if (random(0, 10) == 0) {
    lastSeenSeq++;
    gapCount++;
    addLog("[GAP] expected " + String(lastSeenSeq - 1) + ", got " + String(lastSeenSeq) + "  (simulated)");
  }

  float simulatedTemp = 20.0 + (lastSeenSeq % 15);
  String payload = "SEQ:" + String(lastSeenSeq) + "|TEMP:" + String(simulatedTemp, 1);
  addLog("[RECV-SIM] " + payload + " (RSSI " + String(lastRssi) + ", SNR " + String(lastSnr, 1) + ")");
}

String buildStatusJson() {
  String json = "{";
  json += "\"role\":\"receiver\",";
  json += "\"uptime_s\":" + String(millis() / 1000) + ",";
  json += "\"packets_received\":" + String(packetsReceived) + ",";
  json += "\"gap_count\":" + String(gapCount) + ",";
  json += "\"last_rssi\":" + String(lastRssi) + ",";
  json += "\"last_snr\":" + String(lastSnr, 1) + ",";
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

  if (cmd == "clear_log") {
    packetsReceived = 0;
    lastSeenSeq = 0;
    gapCount = 0;
    for (int i = 0; i < LOG_SIZE; i++) logBuffer[i] = "";
    addLog("[CTRL] log cleared");
    return "{\"result\":\"cleared\"}";
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

  Serial.println("=== MOCK Receiver (dashboard testing, no LoRa) ===");
  randomSeed(analogRead(0));

  Serial.println("Serial transport ready NOW: send STATUS / LOG / ACTION:{...} lines.");
  Serial.println("(WiFi connects in the background - Serial mode does not wait for it.)");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non-blocking: connects in the background

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleOptions);
  server.begin();

  addLog("[BOOT] mock receiver online");
}

void loop() {
  server.handleClient();

  static bool wifiAnnounced = false;
  if (!wifiAnnounced && WiFi.status() == WL_CONNECTED) {
    wifiAnnounced = true;
    Serial.print("WiFi connected. IP address: ");
    Serial.println(WiFi.localIP());
    Serial.println("^^^ Enter this IP into the dashboard's Receiver field (WiFi mode) ^^^");
  }

  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handleSerialLine(line);
  }

  unsigned long now = millis();
  if (now - lastSimTime >= SIM_RECEIVE_INTERVAL_MS) {
    lastSimTime = now;
    simulateReceive();
  }
}
