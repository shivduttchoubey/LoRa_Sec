/*
 * Legitimate Receiver - WiFi + Serial Dashboard Edition
 * -----------------------------------------------------------
 * Board: ESP32 + SX1278
 *
 * Supports BOTH transports - see lora_sender_wifi.ino header for the
 * Serial protocol description.
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

uint32_t packetsReceived = 0;
uint32_t lastSeenSeq = 0;
uint32_t gapCount = 0;
int lastRssi = 0;
float lastSnr = 0;
bool encryptionEnabled = false;

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

  Serial.println("=== Legitimate Receiver (WiFi + Serial Dashboard Edition) ===");

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

  addLog("[BOOT] receiver online");
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

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String received = "";
    while (LoRa.available()) {
      received += (char)LoRa.read();
    }

    lastRssi = LoRa.packetRssi();
    lastSnr = LoRa.packetSnr();
    packetsReceived++;

    int seqStart = received.indexOf("SEQ:") + 4;
    int seqEnd = received.indexOf("|", seqStart);
    long seq = (seqStart > 3 && seqEnd > seqStart)
                 ? received.substring(seqStart, seqEnd).toInt()
                 : -1;

    if (seq > 0) {
      if (lastSeenSeq > 0 && seq != lastSeenSeq + 1) {
        gapCount++;
        addLog("[GAP] expected " + String(lastSeenSeq + 1) + ", got " + String(seq));
      }
      lastSeenSeq = seq;
    }

    addLog("[RECV] " + received + " (RSSI " + String(lastRssi) + ", SNR " + String(lastSnr, 1) + ")");
  }
}
