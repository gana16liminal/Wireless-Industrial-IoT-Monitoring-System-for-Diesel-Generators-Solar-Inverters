This code runs on the LoRaWAN Gateway / ESP32 that receives data from all field nodes, forwards it via MQTT to the cloud, and optionally displays it on a local dashboard.

// ============================================================
// RECEIVER: ESP32 Gateway (LoRa → MQTT)
// Receives LoRa packets from field nodes and forwards to MQTT
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// ------------------- WiFi Configuration -------------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ------------------- MQTT Configuration -------------------
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* mqtt_topic = "field_nodes/data";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ------------------- LoRa Configuration -------------------
#define SS_PIN 5
#define RST_PIN 14
#define DIO0_PIN 26
#define LORA_FREQUENCY 868E6
#define SYNC_WORD 0xF3

// ------------------- Statistics -------------------
uint32_t packetsReceived = 0;
uint32_t packetsWithErrors = 0;
uint32_t lastLogTime = 0;

// ============================================================
// Setup Function
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n=== Gateway Starting ===");

  // 1. Initialize LoRa Radio
  Serial.println("Initializing LoRa...");
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("❌ LoRa init failed!");
    while (1);
  }
  
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.setSpreadingFactor(12);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  
  Serial.println("✅ LoRa initialized successfully!");

  // 2. Connect to WiFi
  setup_wifi();

  // 3. Connect to MQTT Broker
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  
  Serial.println("Gateway ready!");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  // Maintain MQTT connection
  if (!mqttClient.connected()) {
    reconnect_mqtt();
  }
  mqttClient.loop();

  // Check for LoRa packets
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    process_lora_packet(packetSize);
  }

  // Log statistics every 30 seconds
  if (millis() - lastLogTime > 30000) {
    Serial.printf("📊 Stats - Received: %d | Errors: %d\n", 
                  packetsReceived, packetsWithErrors);
    lastLogTime = millis();
  }
}

// ============================================================
// Helper Functions
// ============================================================

// Connect to WiFi
void setup_wifi() {
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
  }
}

// Reconnect to MQTT
void reconnect_mqtt() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "Gateway-";
    clientId += String(random(0xffff), HEX);

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("✅ connected");
    } else {
      Serial.printf("❌ failed, rc=%d, retrying in 5s\n", mqttClient.state());
      delay(5000);
    }
  }
}

// MQTT callback for incoming messages (optional)
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Handle commands from cloud if needed
  Serial.print("📨 MQTT message on topic: ");
  Serial.println(topic);
}

// ----- Process Incoming LoRa Packet -----
void process_lora_packet(int packetSize) {
  packetsReceived++;
  
  // Read packet data
  char buffer[256];
  int index = 0;
  while (LoRa.available() && index < 255) {
    buffer[index++] = (char)LoRa.read();
  }
  buffer[index] = '\0';

  // Check for CRC errors
  if (LoRa.packetRssi() == 0) {
    packetsWithErrors++;
    Serial.println("❌ Packet with CRC error");
    return;
  }

  // Parse JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, buffer);

  if (error) {
    packetsWithErrors++;
    Serial.printf("❌ JSON parse error: %s\n", error.c_str());
    return;
  }

  // Extract data
  int nodeId = doc["node_id"] | -1;
  float voltage = doc["voltage"] | 0.0;
  float current = doc["current"] | 0.0;
  float frequency = doc["frequency"] | 0.0;
  float power = doc["power"] | 0.0;
  uint16_t fault = doc["fault"] | 0;
  uint32_t packetNum = doc["packet"] | 0;

  // Display received data
  Serial.printf("\n📥 Packet #%d from Node %d\n", packetNum, nodeId);
  Serial.printf("   RSSI: %d dBm | SNR: %.1f dB\n", 
                LoRa.packetRssi(), LoRa.packetSnr());
  Serial.printf("   V: %.1f V | I: %.2f A | F: %.1f Hz | P: %.1f W\n", 
                voltage, current, frequency, power);
  Serial.printf("   Fault: 0x%04X\n", fault);

  // Forward to MQTT
  forward_to_mqtt(nodeId, voltage, current, frequency, power, fault, packetNum);
}

// ----- Forward Data to MQTT Broker -----
void forward_to_mqtt(int nodeId, float v, float i, float f, float p, 
                     uint16_t fault, uint32_t packetNum) {
  // Create JSON payload for cloud
  StaticJsonDocument<256> doc;
  doc["node_id"] = nodeId;
  doc["packet"] = packetNum;
  doc["voltage"] = v;
  doc["current"] = i;
  doc["frequency"] = f;
  doc["power"] = p;
  doc["fault_state"] = fault;
  doc["rssi"] = LoRa.packetRssi();
  doc["snr"] = LoRa.packetSnr();
  doc["timestamp"] = millis();

  // Add node name for clarity
  if (nodeId == 1) {
    doc["node_name"] = "Diesel_Generator";
  } else if (nodeId == 2) {
    doc["node_name"] = "Solar_Inverter";
  } else {
    doc["node_name"] = "Unknown";
  }

  char buffer[300];
  size_t n = serializeJson(doc, buffer);

  // Publish to MQTT
  if (mqttClient.connected()) {
    if (mqttClient.publish(mqtt_topic, buffer)) {
      Serial.printf("📤 MQTT forwarded (%d bytes)\n", n);
    } else {
      Serial.println("❌ MQTT publish failed");
    }
  } else {
    Serial.println("⚠️ MQTT not connected, data lost");
  }
}
