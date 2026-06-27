// ============================================================
// Complete Code for ESP32 Modbus-to-LoRaWAN/MQTT Gateway
// Based on: ESP32 | RS-485 (MAX485) | LoRa SX1276 | MQTT
// ============================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <LoRa.h>
#include <ArduinoJson.h>

// ------------------- WiFi & MQTT Configuration -------------------
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "test.mosquitto.org"; // Your MQTT Broker IP
const int mqtt_port = 1883;
const char* mqtt_topic = "field_node/data";

WiFiClient espClient;
PubSubClient client(espClient);

// ------------------- Modbus Configuration -------------------
#define RE_DE_PIN 4 // RS-485 Direction Control Pin
#define RX_PIN 16   // UART RX for RS-485
#define TX_PIN 17   // UART TX for RS-485

// Create Modbus Master object
ModbusMaster node;

// ------------------- LoRa Configuration (SX1276) -------------------
#define SS_PIN 5
#define RST_PIN 14
#define DIO0_PIN 26
#define LORA_FREQUENCY 868E6 // 868 MHz for EU, use 915E6 for US

// ------------------- Variables for Sensor Data -------------------
float voltage = 0.0;
float current = 0.0;
float frequency = 0.0;
float power = 0.0;
uint16_t faultState = 0;

// ============================================================
// Setup Function
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);

  // 1. Initialize RS-485 & Modbus Master
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW); // Set to Receive Mode initially

  node.begin(1, Serial1); // Modbus Slave ID = 1
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // 2. Initialize LoRa Radio
  Serial.println("Initializing LoRa...");
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed. Check wiring.");
    while (1);
  }
  LoRa.setTxPower(14); // TX power in dB (range: 2-20)
  Serial.println("LoRa initialized.");

  // 3. Connect to WiFi
  setup_wifi();

  // 4. Connect to MQTT Broker
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.loop();

  // --- Step 1: Read data from Modbus slave (DG or Solar Inverter) ---
  read_modbus_data();

  // --- Step 2: Send data via LoRa (Field Node to Gateway) ---
  send_lora_packet();

  // --- Step 3: Publish data via MQTT (for cloud/dashboard) ---
  publish_mqtt_data();

  delay(10000); // Wait 10 seconds before next cycle
}

// ============================================================
// Helper Functions
// ============================================================

// Set RS-485 to Transmit mode before sending Modbus query
void preTransmission() {
  digitalWrite(RE_DE_PIN, HIGH);
}

// Set RS-485 back to Receive mode after sending query
void postTransmission() {
  digitalWrite(RE_DE_PIN, LOW);
}

// Connect to Wi-Fi
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT callback (for handling incoming commands, if any)
void callback(char* topic, byte* payload, unsigned int length) {
  // Handle incoming messages from the broker here (e.g., for remote control)
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// Reconnect to MQTT Broker
void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a unique client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Subscribe to topics if needed
      // client.subscribe("command_topic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

// --- Function 1: Modbus RTU Data Acquisition ---
void read_modbus_data() {
  uint8_t result;

  // Read Holding Registers starting at address 0, 4 registers
  // Registers: 0=Voltage, 1=Current, 2=Frequency, 3=Power
  result = node.readHoldingRegisters(0, 4);

  if (result == node.ku8MBSuccess) {
    voltage = node.getResponseBuffer(0) / 10.0;   // Example scaling
    current = node.getResponseBuffer(1) / 100.0;
    frequency = node.getResponseBuffer(2) / 10.0;
    power = node.getResponseBuffer(3) / 1.0;
    Serial.println("Modbus read successful.");
  } else {
    Serial.print("Modbus read failed. Error: 0x");
    Serial.println(result, HEX);
    // Optionally set fault state or default values
    faultState = 0xFFFF;
  }

  // Read a Discrete Input for Fault State (Coil or Discrete Input)
  result = node.readDiscreteInputs(0, 1);
  if (result == node.ku8MBSuccess) {
    faultState = node.getResponseBuffer(0);
  } else {
    faultState = 0xFFFF;
  }
}

// --- Function 2: LoRa Packet Transmission ---
void send_lora_packet() {
  // Create a JSON packet for LoRa transmission
  StaticJsonDocument<200> doc;
  doc["v"] = voltage;
  doc["c"] = current;
  doc["f"] = frequency;
  doc["p"] = power;
  doc["fault"] = faultState;

  char buffer[200];
  size_t n = serializeJson(doc, buffer);

  // Send the packet
  LoRa.beginPacket();
  LoRa.print(buffer);
  LoRa.endPacket();
  Serial.println("LoRa packet sent.");
}

// --- Function 3: MQTT Data Publishing ---
void publish_mqtt_data() {
  // Create a JSON payload for the cloud
  StaticJsonDocument<200> doc;
  doc["voltage"] = voltage;
  doc["current"] = current;
  doc["frequency"] = frequency;
  doc["power"] = power;
  doc["fault_state"] = faultState;
  doc["timestamp"] = millis();

  char buffer[200];
  size_t n = serializeJson(doc, buffer);

  // Publish to the MQTT broker
  if (client.publish(mqtt_topic, buffer)) {
    Serial.println("MQTT message published.");
  } else {
    Serial.println("MQTT publish failed.");
  }
}
