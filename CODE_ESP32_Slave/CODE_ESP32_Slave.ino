#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <Wire.h>
#include <BH1750.h>
#include <U8g2lib.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// WiFi credentials
#define WIFI_SSID "NGOC HIEN"      // Tên mạng WiFi
#define WIFI_PASSWORD "trancaominhkhoi"    // Mật khẩu WiFi

// Firebase credentials
#define API_KEY "AIzaSyAwNLS7RHasQrILtUGDW6BiNZqB5u9U5WY"
#define DATABASE_URL "https://smart-home-d6e89-default-rtdb.firebaseio.com/"
#define USER_EMAIL "nguyentaianhtuan2004@gmail.com"
#define USER_PASSWORD "0123456789"

// Pin definitions - Sensors
#define DHT_PIN 14
#define MQ2_PIN 34
#define SDA_PIN 21
#define SCL_PIN 22

// Pin definitions - Relay 4 channels
#define RELAY1_PIN 16  // Relay kênh 1
#define RELAY2_PIN 17  // Relay kênh 2  
#define RELAY3_PIN 18  // Relay kênh 3 (điều khiển quạt)
#define RELAY4_PIN 19  // Relay kênh 4 (điều khiển đèn)

// DHT22 setup
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// OLED SH1106 setup
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// BH1750 setup
BH1750 lightMeter;

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variables - Sensors
float temperature = 0;
float humidity = 0;
int gasValue = 0;
float lightLevel = 0;
unsigned long sendDataPrevMillis = 0;
unsigned long sensorReadPrevMillis = 0;
bool debugMode = false;
int globalRecordCounter = 0;

// Variables - Relay control
bool relay1State = false;
bool relay2State = false;
bool relay3State = false;
bool relay4State = false;
bool relay3AutoMode = true;
bool relay4AutoMode = true;
unsigned long relayCheckPrevMillis = 0;

// Variables - OLED Display Pages
int currentPage = 0;
unsigned long pageStartTime = 0;
const unsigned long PAGE_DISPLAY_TIME = 4000; // 4 seconds per page
bool displayNeedsUpdate = true;

// Variables - Connection monitoring
unsigned long wifiCheckPrevMillis = 0;
unsigned long firebaseCheckPrevMillis = 0;
bool wifiConnected = false;
bool firebaseConnected = false;

// Thresholds and intervals
const int GAS_THRESHOLD = 4000;
const unsigned long SEND_INTERVAL = 5000;
const unsigned long RELAY_CHECK_INTERVAL = 2000;
const unsigned long SENSOR_READ_INTERVAL = 1000;  // Read sensors every 1 second
const unsigned long WIFI_CHECK_INTERVAL = 30000;  // Check WiFi every 30 seconds
const unsigned long FIREBASE_CHECK_INTERVAL = 60000; // Check Firebase every 60 seconds

// Light and temperature thresholds
#define LIGHT_NIGHT 100
#define TEMP_COOL 24
#define TEMP_HOT 25

// Hysteresis for relay control (prevent flickering)
#define TEMP_HYSTERESIS 0.5
#define LIGHT_HYSTERESIS 10

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== ESP32 Smart Home System Starting ===");
  
  // Initialize relay pins
  initializeRelayPins();
  
  // Initialize I2C and sensors
  initializeSensors();
  
  // Initialize OLED display
  initializeDisplay();
  
  // Test all relays
  testRelays();
  
  // Connect to WiFi
  connectToWiFi();
  
  // Configure and connect Firebase
  configureFirebase();
  
  // Initialize system states
  initializeSystemStates();
  
  Serial.println("✅ Setup complete!");
  Serial.println("=== MONITORING STARTED ===");
}

void loop() {
  // Monitor connections
  monitorConnections();
  
  // Read sensors (with interval)
  if (millis() - sensorReadPrevMillis >= SENSOR_READ_INTERVAL) {
    sensorReadPrevMillis = millis();
    readSensors();
    displayNeedsUpdate = true;
  }
  
  // Control relays in auto mode
  controlRelaysAuto();
  
  // Update OLED display
  updateDisplay();
  
  // Check relay commands from Firebase
  if (Firebase.ready() && (millis() - relayCheckPrevMillis >= RELAY_CHECK_INTERVAL)) {
    relayCheckPrevMillis = millis();
    checkRelayCommands();
  }
  
  // Send data to Firebase
  if (Firebase.ready() && (millis() - sendDataPrevMillis >= SEND_INTERVAL)) {
    sendDataPrevMillis = millis();
    sendToFirebase();
  }
  
  delay(100); // Small delay to prevent CPU overload
}

void initializeRelayPins() {
  Serial.println("🔧 Initializing relay pins...");
  
  int relayPins[] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
  for (int i = 0; i < 4; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW); // All relays OFF
    Serial.printf("   Relay %d: GPIO %d - OFF\n", i+1, relayPins[i]);
  }
  
  Serial.println("✅ All relays initialized (OFF)");
}

void initializeSensors() {
  Serial.println("🔧 Initializing sensors...");
  
  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("   I2C initialized");
  
  // Initialize DHT22
  dht.begin();
  Serial.println("   DHT22 initialized");
  
  // Initialize BH1750
  if (lightMeter.begin()) {
    Serial.println("   BH1750 initialized");
  } else {
    Serial.println("   ❌ Error initializing BH1750");
  }
  
  Serial.println("✅ Sensors initialized");
}

void initializeDisplay() {
  Serial.println("🔧 Initializing OLED display...");
  
  display.begin();
  display.enableUTF8Print();
  showBootMessage("Khoi dong he thong...");
  
  Serial.println("✅ OLED SH1106 initialized");
}

void showBootMessage(const char* message) {
  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.setCursor(0, 15);
  display.print(message);
  display.setCursor(0, 35);
  display.print("Smart Home v2.0");
  display.setCursor(0, 55);
  display.print("Vui long cho...");
  display.sendBuffer();
  delay(2000);
}

void testRelays() {
  Serial.println("🔧 Testing all relays...");
  showBootMessage("Test relay...");
  
  int relayPins[] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
  
  for (int i = 0; i < 4; i++) {
    Serial.printf("   Testing Relay %d (GPIO %d)...\n", i+1, relayPins[i]);
    digitalWrite(relayPins[i], HIGH);
    delay(300);
    digitalWrite(relayPins[i], LOW);
    delay(200);
  }
  
  Serial.println("✅ All relays test completed");
}

void connectToWiFi() {
  Serial.println("🔧 Connecting to WiFi...");
  showBootMessage("Ket noi WiFi...");
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\n✅ WiFi connected!");
    Serial.printf("   IP Address: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("   Signal Strength: %d dBm\n", WiFi.RSSI());
  } else {
    wifiConnected = false;
    Serial.println("\n❌ WiFi connection failed!");
  }
}

void configureFirebase() {
  Serial.println("🔧 Configuring Firebase...");
  showBootMessage("Ket noi Firebase...");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  
  // Wait for Firebase to be ready
  int attempts = 0;
  while (!Firebase.ready() && attempts < 10) {
    Serial.print(".");
    delay(1000);
    attempts++;
  }
  
  if (Firebase.ready()) {
    firebaseConnected = true;
    Serial.println("\n✅ Firebase connected!");
  } else {
    firebaseConnected = false;
    Serial.println("\n❌ Firebase connection failed!");
  }
}

void initializeSystemStates() {
  Serial.println("🔧 Initializing system states...");
  
  if (Firebase.ready()) {
    // Initialize relay states in Firebase
    Firebase.setInt(fbdo, "/controls/relay1", 0);
    Firebase.setInt(fbdo, "/controls/relay2", 0);
    Firebase.setInt(fbdo, "/controls/relay3", 0);
    Firebase.setInt(fbdo, "/controls/relay4", 0);
    Firebase.setInt(fbdo, "/controls/relay3_auto", 1);
    Firebase.setInt(fbdo, "/controls/relay4_auto", 1);
    
    // Send system info
    Firebase.setString(fbdo, "/system/version", "Smart Home v2.0");
    Firebase.setString(fbdo, "/system/chip_model", "ESP32");
    Firebase.setInt(fbdo, "/system/free_heap", ESP.getFreeHeap());
    
    Serial.println("✅ System states initialized");
  }
}

void monitorConnections() {
  // Monitor WiFi connection
  if (millis() - wifiCheckPrevMillis >= WIFI_CHECK_INTERVAL) {
    wifiCheckPrevMillis = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      if (wifiConnected) {
        Serial.println("❌ WiFi disconnected! Attempting to reconnect...");
        wifiConnected = false;
      }
      WiFi.reconnect();
    } else {
      if (!wifiConnected) {
        Serial.println("✅ WiFi reconnected!");
        wifiConnected = true;
      }
    }
  }
  
  // Monitor Firebase connection
  if (millis() - firebaseCheckPrevMillis >= FIREBASE_CHECK_INTERVAL) {
    firebaseCheckPrevMillis = millis();
    
    if (Firebase.ready()) {
      if (!firebaseConnected) {
        Serial.println("✅ Firebase reconnected!");
        firebaseConnected = true;
      }
    } else {
      if (firebaseConnected) {
        Serial.println("❌ Firebase disconnected!");
        firebaseConnected = false;
      }
    }
  }
}

void controlRelaysAuto() {
  // Control relay3 (fan) with hysteresis
  if (relay3AutoMode) {
    bool newState = relay3State;
    
    if (!relay3State && temperature > (TEMP_HOT + TEMP_HYSTERESIS)) {
      newState = true; // Turn ON fan
    } else if (relay3State && temperature < (TEMP_COOL - TEMP_HYSTERESIS)) {
      newState = false; // Turn OFF fan
    }
    
    if (newState != relay3State) {
      relay3State = newState;
      digitalWrite(RELAY3_PIN, relay3State ? HIGH : LOW);
      Serial.printf("🔌 Relay 3 AUTO %s (Temp: %.1f°C)\n", 
                    relay3State ? "ON" : "OFF", temperature);
    }
  }
  
  // Control relay4 (light) with hysteresis
  if (relay4AutoMode) {
    bool newState = relay4State;
    
    if (!relay4State && lightLevel < (LIGHT_NIGHT - LIGHT_HYSTERESIS)) {
      newState = true; // Turn ON light
    } else if (relay4State && lightLevel > (LIGHT_NIGHT + LIGHT_HYSTERESIS)) {
      newState = false; // Turn OFF light
    }
    
    if (newState != relay4State) {
      relay4State = newState;
      digitalWrite(RELAY4_PIN, relay4State ? HIGH : LOW);
      Serial.printf("🔌 Relay 4 AUTO %s (Light: %.1f lux)\n", 
                    relay4State ? "ON" : "OFF", lightLevel);
    }
  }
}

void checkRelayCommands() {
  // Check all relay commands efficiently
  int relayCommands[4];
  bool relayStates[4] = {relay1State, relay2State, relay3State, relay4State};
  int relayPins[4] = {RELAY1_PIN, RELAY2_PIN, RELAY3_PIN, RELAY4_PIN};
  
  // Get relay commands
  for (int i = 0; i < 4; i++) {
    String path = "/controls/relay" + String(i+1);
    if (Firebase.getInt(fbdo, path)) {
      relayCommands[i] = fbdo.intData();
    }
  }
  
  // Update relay1 and relay2 (always manual)
  for (int i = 0; i < 2; i++) {
    bool newState = (relayCommands[i] == 1);
    if (newState != relayStates[i]) {
      relayStates[i] = newState;
      digitalWrite(relayPins[i], relayStates[i] ? HIGH : LOW);
      Serial.printf("🔌 Relay %d %s\n", i+1, relayStates[i] ? "ON" : "OFF");
    }
  }
  
  // Update relay states
  relay1State = relayStates[0];
  relay2State = relayStates[1];
  
  // Check auto mode for relay3 and relay4
  if (Firebase.getInt(fbdo, "/controls/relay3_auto")) {
    relay3AutoMode = (fbdo.intData() == 1);
  }
  if (Firebase.getInt(fbdo, "/controls/relay4_auto")) {
    relay4AutoMode = (fbdo.intData() == 1);
  }
  
  // Manual control for relay3 and relay4 when not in auto mode
  if (!relay3AutoMode && Firebase.getInt(fbdo, "/controls/relay3")) {
    bool newState = (fbdo.intData() == 1);
    if (newState != relay3State) {
      relay3State = newState;
      digitalWrite(RELAY3_PIN, relay3State ? HIGH : LOW);
      Serial.printf("🔌 Relay 3 MANUAL %s\n", relay3State ? "ON" : "OFF");
    }
  }
  
  if (!relay4AutoMode && Firebase.getInt(fbdo, "/controls/relay4")) {
    bool newState = (fbdo.intData() == 1);
    if (newState != relay4State) {
      relay4State = newState;
      digitalWrite(RELAY4_PIN, relay4State ? HIGH : LOW);
      Serial.printf("🔌 Relay 4 MANUAL %s\n", relay4State ? "ON" : "OFF");
    }
  }
}

void readSensors() {
  // Read DHT22 with error handling
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();
  
  if (!isnan(newTemp) && !isnan(newHum)) {
    temperature = newTemp;
    humidity = newHum;
  } else {
    Serial.println("❌ DHT22 read error - using last valid values");
  }
  
  // Read MQ2 with averaging
  long totalRaw = 0;
  for(int i = 0; i < 10; i++) {
    totalRaw += analogRead(MQ2_PIN);
    delay(5);
  }
  int avgRaw = totalRaw / 10;
  gasValue = map(avgRaw, 0, 4095, 0, 10000);
  
  // Read BH1750 with error handling
  float newLight = lightMeter.readLightLevel();
  if (newLight >= 0) {
    lightLevel = newLight;
  } else {
    Serial.println("❌ BH1750 read error - using last valid value");
  }
  
  // Periodic sensor status
  static unsigned long lastSensorLog = 0;
  if (millis() - lastSensorLog > 30000) { // Every 30 seconds
    lastSensorLog = millis();
    Serial.printf("📊 Sensors - T:%.1f°C H:%.1f%% Gas:%dppm Light:%.1flux\n", 
                  temperature, humidity, gasValue, lightLevel);
  }
}

void updateDisplay() {
  // Check if it's time to switch pages
  if (millis() - pageStartTime >= PAGE_DISPLAY_TIME) {
    pageStartTime = millis();
    currentPage = (currentPage + 1) % 3;
    displayNeedsUpdate = true;
  }
  
  // Only update display if needed
  if (displayNeedsUpdate) {
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tf);
    
    switch (currentPage) {
      case 0:
        displayPage1_SensorData();
        break;
      case 1:
        displayPage2_DeviceStatus();
        break;
      case 2:
        displayPage3_AutoManualMode();
        break;
    }
    
    display.sendBuffer();
    displayNeedsUpdate = false;
  }
}

void displayPage1_SensorData() {
  // Header
  display.setFont(u8g2_font_6x10_tf);
  display.setCursor(0, 10);
  display.print("=THONG SO CAM BIEN=");
  
  // Sensor data
  display.setCursor(0, 22);
  display.printf("Nhiet do: %.1f°C", temperature);
  
  display.setCursor(0, 32);
  display.printf("Do am: %.1f%%", humidity);
  
  display.setCursor(0, 42);
  display.printf("Gas: %d ppm", gasValue);
  
  display.setCursor(0, 52);
  display.printf("Anh sang: %.0f lux", lightLevel);
  
  // Status
  display.setCursor(0, 62);
  if (gasValue >= GAS_THRESHOLD) {
    display.print("*** NGUY HIEM ***");
  } else {
    display.print("Trang thai: AN TOAN");
  }
}

void displayPage2_DeviceStatus() {
  // Header
  display.setCursor(0, 10);
  display.print("=TRANG THAI THIET BI=");
  
  // Relay status
  display.setCursor(0, 22);
  display.printf("R1:%s  R2:%s", relay1State ? "ON " : "OFF", relay2State ? "ON " : "OFF");
  
  display.setCursor(0, 32);
  display.printf("R3:%s  R4:%s", relay3State ? "ON " : "OFF", relay4State ? "ON " : "OFF");
  
  display.setCursor(0, 42);
  display.printf("Quat: %s", relay3State ? "HOAT DONG" : "TAT");
  
  display.setCursor(0, 52);
  display.printf("Den: %s", relay4State ? "HOAT DONG" : "TAT");
  
  // Connection status
  display.setCursor(0, 62);
  display.printf("WiFi:%s FB:%s", 
                wifiConnected ? "OK" : "X", 
                firebaseConnected ? "OK" : "X");
}

void displayPage3_AutoManualMode() {
  // Header
  display.setCursor(0, 10);
  display.print("=CHE DO DIEU KHIEN=");
  
  // Control modes
  display.setCursor(0, 22);
  display.print("R1: MANUAL  R2: MANUAL");
  
  display.setCursor(0, 32);
  display.printf("R3: %-7s R4: %-7s", 
                relay3AutoMode ? "AUTO" : "MANUAL",
                relay4AutoMode ? "AUTO" : "MANUAL");
  
  // Auto conditions
  display.setCursor(0, 42);
  if (relay3AutoMode) {
    display.printf("Quat: %s (%.1f°C)", 
                  temperature > TEMP_HOT ? "Nong" : "Mat", temperature);
  } else {
    display.print("Quat: MANUAL");
  }
  
  display.setCursor(0, 52);
  if (relay4AutoMode) {
    display.printf("Den: %s (%.0flux)", 
                  lightLevel <= LIGHT_NIGHT ? "Toi" : "Sang", lightLevel);
  } else {
    display.print("Den: MANUAL");
  }
  
  // System info
  display.setCursor(0, 62);
  display.printf("Heap: %dKB", ESP.getFreeHeap() / 1024);
}

void sendToFirebase() {
  if (!Firebase.ready()) return;
  
  String timestamp = String(millis());
  String lightStatus = (lightLevel <= LIGHT_NIGHT) ? "Ban dem" : "Ban ngay";
  String fanStatus = (temperature > TEMP_HOT) ? "Nong" : "Mat";
  
  // Send individual sensor readings
  Firebase.setFloat(fbdo, "/sensors/temperature", temperature);
  Firebase.setFloat(fbdo, "/sensors/humidity", humidity);
  Firebase.setInt(fbdo, "/sensors/gas", gasValue);
  Firebase.setFloat(fbdo, "/sensors/light", lightLevel);
  Firebase.setString(fbdo, "/sensors/light_status", lightStatus);
  Firebase.setString(fbdo, "/sensors/fan_status", fanStatus);
  Firebase.setString(fbdo, "/sensors/last_update", timestamp);
  
  // Send relay states
  Firebase.setInt(fbdo, "/status/relay1", relay1State ? 1 : 0);
  Firebase.setInt(fbdo, "/status/relay2", relay2State ? 1 : 0);
  Firebase.setInt(fbdo, "/status/relay3", relay3State ? 1 : 0);
  Firebase.setInt(fbdo, "/status/relay4", relay4State ? 1 : 0);
  Firebase.setInt(fbdo, "/status/relay3_auto", relay3AutoMode ? 1 : 0);
  Firebase.setInt(fbdo, "/status/relay4_auto", relay4AutoMode ? 1 : 0);
  
  // Send system status
  Firebase.setInt(fbdo, "/system/uptime", millis() / 1000);
  Firebase.setInt(fbdo, "/system/free_heap", ESP.getFreeHeap());
  Firebase.setInt(fbdo, "/system/wifi_rssi", WiFi.RSSI());
  Firebase.setInt(fbdo, "/system/wifi_connected", wifiConnected ? 1 : 0);
  Firebase.setInt(fbdo, "/system/firebase_connected", firebaseConnected ? 1 : 0);
  
  // Send complete data to history
  globalRecordCounter++;
  FirebaseJson json;
  json.set("temperature", temperature);
  json.set("humidity", humidity);
  json.set("gas", gasValue);
  json.set("light", lightLevel);
  json.set("light_status", lightStatus);
  json.set("fan_status", fanStatus);
  json.set("timestamp", timestamp);
  json.set("gas_alert", gasValue >= GAS_THRESHOLD);
  json.set("record_id", globalRecordCounter);
  json.set("relay1_state", relay1State ? 1 : 0);
  json.set("relay2_state", relay2State ? 1 : 0);
  json.set("relay3_state", relay3State ? 1 : 0);
  json.set("relay4_state", relay4State ? 1 : 0);
  json.set("relay3_auto", relay3AutoMode ? 1 : 0);
  json.set("relay4_auto", relay4AutoMode ? 1 : 0);
  json.set("uptime", millis() / 1000);
  json.set("free_heap", ESP.getFreeHeap());
  json.set("wifi_rssi", WiFi.RSSI());
  
  String path = "/sensor_history/record_" + String(globalRecordCounter);
  if (Firebase.setJSON(fbdo, path, json)) {
    Serial.printf("✅ Data sent to Firebase - Record #%d\n", globalRecordCounter);
  } else {
    Serial.printf("❌ Failed to send data: %s\n", fbdo.errorReason().c_str());
  }
  
  // Clean up old records (keep only latest 10)
  if (globalRecordCounter > 10) {
    int deleteIndex = globalRecordCounter - 10;
    String pathToDelete = "/sensor_history/record_" + String(deleteIndex);
    Firebase.deleteNode(fbdo, pathToDelete);
  }
}