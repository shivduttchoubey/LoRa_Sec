/*
 * Legitimate Sender - WiFi + Serial Dashboard Edition
 * -------------------------------------------------------
 * Board: ESP32 + SX1276
 *
 * Supports BOTH transports so the dashboard can reach this board over
 * WiFi (HTTP) or USB (Serial) - use Serial as a presentation-day
 * fallback if WiFi misbehaves.
 *
 * WiFi endpoints:   GET /status, GET /log, POST /action
 * Serial protocol:  send "STATUS" | "LOG" | "ACTION:<json>" + newline
 *                    reply is always "RESP:<json>" + newline
 *
 * Fill in WIFI_SSID / WIFI_PASSWORD below before flashing.
 */

#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <WebServer.h>

const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_NSS   5
#define LORA_RST   14
#define LORA_DIO0  2

#define LORA_FREQUENCY   433E6
#define LORA_SF          7
#define LORA_BANDWIDTH   125E3
#define LORA_CODING_RATE 5

WebServer server(80);

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

void sendPacket() {
  packetCounter++;
  float simulatedTemp = 20.0 + (packetCounter % 15);
  String payload = "SEQ:" + String(packetCounter) + "|TEMP:" + String(simulatedTemp, 1);

  LoRa.beginPacket();
  LoRa.print(payload);
  LoRa.endPacket();

  addLog("[SENT] #" + String(packetCounter) + " -> " + payload);
}

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
    sendPacket();
    return "{\"result\":\"sent\"}";
  } else if (cmd == "send_custom") {
    String payload = extractJsonString(body, "payload");
    if (payload.length() == 0) return "{\"error\":\"empty payload\"}";
    packetCounter++;
    LoRa.beginPacket();
    LoRa.print(payload);
    LoRa.endPacket();
    addLog("[SENT-CUSTOM] #" + String(packetCounter) + " -> " + payload);
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

  Serial.println("=== Legitimate Sender (WiFi + Serial Dashboard Edition) ===");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("ERROR: LoRa init failed. Check wiring.");
    while (true) { delay(1000); }
  }
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODING_RATE);

  Serial.println("Serial transport ready NOW: send STATUS / LOG / ACTION:{...} lines.");
  Serial.println("(WiFi connects in the background - Serial mode does not wait for it.)");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // non-blocking: connects in the background

  server.on("/status", HTTP_GET, handleStatus);
  server.on("/log", HTTP_GET, handleLog);
  server.on("/action", HTTP_POST, handleAction);
  server.on("/action", HTTP_OPTIONS, handleOptions);
  server.begin();

  addLog("[BOOT] sender online");
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
      sendPacket();
    }
  }
}
