/* Smart RFID Lock — add/delete/fetch via MQTT
   Topics:
     - Commands (web -> ESP32):   smartlock/esp32/cmd
         payloads: "addTag" | "deleteTag:<UID>" | "fetchTags" | "OPEN" ...
     - Events (ESP32 -> web):     smartlock/esp32/events
         payloads: "tagAdded:<UID>" | "tagAddFailed" | "tagDeleted:<UID>" | "tagDeleteFailed"
     - Tags (ESP32 -> web):       smartlock/esp32/tags
         payload: JSON: {"count":N,"tags":["UID1","UID2",...]}
*/
#include <vector>

#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

// ----------------- CONFIG -----------------
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASS";

// MQTT
String mqttServer = "broker.hivemq.com";  // override if needed
uint16_t mqttPort = 1883;
const char *TOPIC_CMD = "smartlock/esp32/cmd";
const char *TOPIC_EVENTS = "smartlock/esp32/events";
const char *TOPIC_TAGS = "smartlock/esp32/tags";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Pins (adjust if needed)
#define RELAY_PIN 26  // safe pin for relay
#define BUZZER_PIN 25
#define WIFI_LED_PIN 14
#define SWITCH_PIN 27

// RFID (MFRC522) VSPI pins
#define RST_PIN 15
#define SS_PIN 5
#define MOSI_PIN 23
#define MISO_PIN 19
#define SCK_PIN 18

// OLED (optional)
#define OLED_SDA 21
#define OLED_SCL 22
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

// MFRC522
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Preferences (NVS) storage
Preferences prefs;
const char *PREF_NS = "rfid_v1";
const char *PREF_KEY_COUNT = "count";

// Limits
const uint8_t MAX_TAGS = 50;

// Add-mode behavior
bool addMode = false;
unsigned long addModeStart = 0;
const unsigned long ADD_MODE_TIMEOUT_MS = 20000UL;  // wait 20s for a scanned card

// Helpers
unsigned long lastReconnectAttempt = 0;

// ----------------- Constants used earlier -----------------
const unsigned long UNLOCK_DURATION_MS = 5000UL;

// ----------------- Forward declarations -----------------
void setupWiFi();
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool mqttConnect();
String uidToString(MFRC522::Uid uid);
bool tagExists(const String &uid);
bool addTagToStorage(const String &uid);
bool removeTagFromStorage(const String &uid);
void publishTagsJSON();
void publishEvent(const char *msg);
void beepSuccess();
void beepFail();
void enterAddMode();
void checkAddMode();
String readTagFromIndex(int idx);
void saveTagAtIndex(int idx, const String &uid);
int getTagCount();
void setTagCount(int n);

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // locked
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(WIFI_LED_PIN, OUTPUT);
  digitalWrite(WIFI_LED_PIN, LOW);
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  // OLED init (optional)
  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Lock");
    display.display();
  }

  // RFID
  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();

  // Preferences NVS
  prefs.begin(PREF_NS, false);
  if (prefs.getUInt(PREF_KEY_COUNT, UINT32_MAX) == UINT32_MAX) {
    prefs.putUInt(PREF_KEY_COUNT, 0);
  }

  setupWiFi();

  mqttClient.setServer(mqttServer.c_str(), mqttPort);
  mqttClient.setCallback(mqttCallback);

  // initial connect attempt
  mqttConnect();
  publishTagsJSON();  // publish existing tags at boot
}

// ----------------- Loop -----------------
void loop() {
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      mqttConnect();
    }
  } else {
    mqttClient.loop();
  }

  // check add mode scanning
  if (addMode) checkAddMode();

  // physical switch (optional): open door if pressed
  if (digitalRead(SWITCH_PIN) == LOW) {
    // simple unlock pulse
    digitalWrite(RELAY_PIN, HIGH);
    delay(2500);
    digitalWrite(RELAY_PIN, LOW);
    beepSuccess();
    mqttClient.publish(TOPIC_EVENTS, "OpenedBySwitch");
    delay(300);  // debounce-ish
  }
}

// ----------------- WiFi & MQTT -----------------
void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    digitalWrite(WIFI_LED_PIN, !digitalRead(WIFI_LED_PIN));
    delay(300);
  }
  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(WIFI_LED_PIN, HIGH);
    Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
  } else {
    digitalWrite(WIFI_LED_PIN, LOW);
    Serial.println("WiFi not connected");
  }
}

bool mqttConnect() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (mqttClient.connected()) return true;

  String clientId = "ESP32Lock-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("Connecting to MQTT... ");
  if (mqttClient.connect(clientId.c_str())) {
    Serial.println("connected");
    mqttClient.subscribe(TOPIC_CMD);
    // publish online
    mqttClient.publish(TOPIC_EVENTS, "Online");
    publishTagsJSON();
    return true;
  } else {
    Serial.printf("failed rc=%d\n", mqttClient.state());
    return false;
  }
}

// ----------------- MQTT message handling -----------------
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.printf("MQTT recv on %s: %s\n", topic, msg.c_str());

  if (String(topic) == TOPIC_CMD) {
    if (msg.equalsIgnoreCase("addTag")) {
      enterAddMode();
    } else if (msg.startsWith("deleteTag:")) {
      String uid = msg.substring(strlen("deleteTag:"));
      uid.trim();
      if (removeTagFromStorage(uid)) {
        publishEvent(("tagDeleted:" + uid).c_str());
        publishTagsJSON();
      } else {
        publishEvent("tagDeleteFailed");
      }
    } else if (msg.equalsIgnoreCase("fetchTags")) {
      publishTagsJSON();
    } else if (msg.equalsIgnoreCase("OPEN")) {
      // optional: open door via mqtt
      digitalWrite(RELAY_PIN, HIGH);
      delay(UNLOCK_DURATION_MS);
      digitalWrite(RELAY_PIN, LOW);
      mqttClient.publish(TOPIC_EVENTS, "OpenedByMQTT");
    }
  }
}

// ----------------- Add mode -----------------
void enterAddMode() {
  addMode = true;
  addModeStart = millis();
  Serial.println("Entered ADD MODE: waiting for card");

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Add mode: Scan tag");
  display.display();

  mqttClient.publish(TOPIC_EVENTS, "AddModeStarted");
}

void checkAddMode() {
  // If card present, read and add
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String uid = uidToString(mfrc522.uid);
    Serial.printf("Card read during add mode: %s\n", uid.c_str());
    if (addTagToStorage(uid)) {
      publishEvent(("tagAdded:" + uid).c_str());
      publishTagsJSON();
      beepSuccess();
    } else {
      publishEvent("tagAddFailed");
      beepFail();
    }
    // exit add mode
    addMode = false;
    mfrc522.PICC_HaltA();
    delay(300);
    return;
  }

  // timeout check
  if (millis() - addModeStart > ADD_MODE_TIMEOUT_MS) {
    addMode = false;
    Serial.println("Add mode timed out");
    publishEvent("tagAddFailed");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Add timed out");
    display.display();
  }
}

// ----------------- Tag storage using Preferences -----------------
String uidKey(int idx) {
  return String("tag") + String(idx);
}

int getTagCount() {
  return prefs.getUInt(PREF_KEY_COUNT, 0);
}

void setTagCount(int n) {
  prefs.putUInt(PREF_KEY_COUNT, n);
}

String readTagFromIndex(int idx) {
  String key = uidKey(idx);
  return prefs.getString(key.c_str(), "");
}

void saveTagAtIndex(int idx, const String &uid) {
  String key = uidKey(idx);
  prefs.putString(key.c_str(), uid);
}

bool tagExists(const String &uid) {
  int cnt = getTagCount();
  for (int i = 0; i < cnt; ++i) {
    if (readTagFromIndex(i).equalsIgnoreCase(uid)) return true;
  }
  return false;
}

bool addTagToStorage(const String &uid) {
  if (uid.length() == 0) return false;
  if (tagExists(uid)) {
    Serial.println("Tag already exists");
    return false;
  }
  int cnt = getTagCount();
  if (cnt >= MAX_TAGS) {
    Serial.println("Tag storage full");
    return false;
  }
  saveTagAtIndex(cnt, uid);
  setTagCount(cnt + 1);
  Serial.printf("Stored tag %s at index %d\n", uid.c_str(), cnt);
  return true;
}

bool removeTagFromStorage(const String &uid) {
  int cnt = getTagCount();
  if (cnt == 0) return false;
  // build new list without uid
  std::vector<String> list;
  for (int i = 0; i < cnt; ++i) {
    String v = readTagFromIndex(i);
    if (!v.equalsIgnoreCase(uid)) list.push_back(v);
  }
  // rewrite
  for (int i = 0; i < cnt; ++i) {
    String key = uidKey(i);
    prefs.remove(key.c_str());
  }
  for (size_t i = 0; i < list.size(); ++i) {
    saveTagAtIndex(i, list[i]);
  }
  setTagCount(list.size());
  Serial.printf("Removed tag %s\n", uid.c_str());
  return true;
}

// ----------------- Publishing -----------------
void publishEvent(const char *msg) {
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_EVENTS, msg);
    Serial.printf("Published event: %s\n", msg);
  }
}

void publishTagsJSON() {
  if (!mqttClient.connected()) return;
  int cnt = getTagCount();
  String payload = "{ \"count\": " + String(cnt) + ", \"tags\": [";
  for (int i = 0; i < cnt; ++i) {
    String t = readTagFromIndex(i);
    payload += "\"" + t + "\"";
    if (i < cnt - 1) payload += ",";
  }
  payload += "] }";
  mqttClient.publish(TOPIC_TAGS, payload.c_str());
  Serial.printf("Published tags: %s\n", payload.c_str());
}

// ----------------- Utilities -----------------
String uidToString(MFRC522::Uid uid) {
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) s += "0";
    s += String(uid.uidByte[i], HEX);
  }
  s.toUpperCase();
  return s;
}

void beepSuccess() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(80);
  digitalWrite(BUZZER_PIN, LOW);
  delay(60);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(80);
  digitalWrite(BUZZER_PIN, LOW);
}

void beepFail() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(250);
  digitalWrite(BUZZER_PIN, LOW);
}
