This code runs on the ESP32 at the Diesel Generator or Solar Inverter location. It reads Modbus data from the device and transmits it via LoRa.

// ============================================================
// TRANSMITTER: ESP32 Field Node (DG or Solar Inverter)
// Reads Modbus RTU data and sends via LoRa
// ============================================================

#include <ModbusMaster.h>
#include <LoRa.h> 
#include <ArduinoJson.h>

// ------------------- RS-485 Configuration -------------------
#define RE_DE_PIN 4      // RS-485 Direction Control
#define RX_PIN 16        // UART RX
#define TX_PIN 17        // UART TX
#define SLAVE_ID 1       // Modbus Slave ID (change for each device)

// ------------------- LoRa Configuration -------------------
#define SS_PIN 5
#define RST_PIN 14
#define DIO0_PIN 26
#define LORA_FREQUENCY 868E6  // 868 MHz (EU) / 915E6 (US)
#define SYNC_WORD 0xF3        // Unique network identifier

// ------------------- Node Identification -------------------
#define NODE_ID 1             // 1 = DG, 2 = Solar Inverter

// Create Modbus Master object
ModbusMaster node;

// ------------------- Variables for Sensor Data -------------------
float voltage = 0.0;
float current = 0.0;
float frequency = 0.0;
float power = 0.0;
uint16_t faultState = 0;
uint32_t packetCounter = 0;

// ============================================================
// Setup Function
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("\n=== Field Node Starting ===");

  // 1. Initialize RS-485 & Modbus Master
  Serial1.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(RE_DE_PIN, OUTPUT);
  digitalWrite(RE_DE_PIN, LOW); // Receive mode initially

  node.begin(SLAVE_ID, Serial1);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  // 2. Initialize LoRa Radio
  Serial.println("Initializing LoRa...");
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa init failed!");
    while (1);
  }
  
  LoRa.setSyncWord(SYNC_WORD);
  LoRa.setTxPower(14);           // 2-20 dB
  LoRa.setSpreadingFactor(12);   // Higher SF = longer range, slower
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  
  Serial.println("LoRa initialized successfully!");
}

// ============================================================
// Main Loop
// ============================================================
void loop() {
  // Step 1: Read data from Modbus slave
  read_modbus_data();

  // Step 2: Send data via LoRa
  send_lora_packet();

  // Step 3: Wait before next reading
  delay(10000); // 10 seconds
}

// ============================================================
// Helper Functions
// ============================================================

// RS-485 direction control
void preTransmission() {
  digitalWrite(RE_DE_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(RE_DE_PIN, LOW);
}

// ----- Modbus Data Reading -----
void read_modbus_data() {
  uint8_t result;

  // Read 4 Holding Registers: Voltage, Current, Frequency, Power
  result = node.readHoldingRegisters(0, 4);

  if (result == node.ku8MBSuccess) {
    voltage = node.getResponseBuffer(0) / 10.0;    // e.g., 230.4 V
    current = node.getResponseBuffer(1) / 100.0;   // e.g., 5.23 A
    frequency = node.getResponseBuffer(2) / 10.0;  // e.g., 50.0 Hz
    power = node.getResponseBuffer(3) / 1.0;       // e.g., 1200 W
    
    Serial.println("✅ Modbus read successful");
    Serial.printf("V: %.1f V | I: %.2f A | F: %.1f Hz | P: %.1f W\n", 
                  voltage, current, frequency, power);
  } else {
    Serial.printf("❌ Modbus read failed. Error: 0x%02X\n", result);
    // Set error flags
    voltage = -1.0;
    current = -1.0;
    frequency = -1.0;
    power = -1.0;
  }

  // Read Fault State (Discrete Input)
  result = node.readDiscreteInputs(0, 1);
  if (result == node.ku8MBSuccess) {
    faultState = node.getResponseBuffer(0);
  } else {
    faultState = 0xFFFF; // Error indicator
  }
}

// ----- LoRa Packet Transmission -----
void send_lora_packet() {
  // Create JSON payload
  StaticJsonDocument<256> doc;
  doc["node_id"] = NODE_ID;          // 1=DG, 2=Solar
  doc["packet"] = packetCounter++;
  doc["voltage"] = voltage;
  doc["current"] = current;
  doc["frequency"] = frequency;
  doc["power"] = power;
  doc["fault"] = faultState;
  doc["timestamp"] = millis();

  char buffer[256];
  size_t n = serializeJson(doc, buffer);

  // Send LoRa packet
  LoRa.beginPacket();
  LoRa.print(buffer);
  LoRa.endPacket();

  Serial.printf("📡 LoRa packet sent (size: %d bytes)\n", n);
  Serial.println(buffer);
}
