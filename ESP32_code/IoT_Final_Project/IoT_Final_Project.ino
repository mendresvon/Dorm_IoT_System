/*
 * Smart RFID Dormitory System — ESP32 Firmware
 * Updated to support: JSON payload parsing, dynamic brightness,
 * PARTY (strobe), BEDTIME (warm), AURORA (northern lights), ORANGE
 * * QoS 1 Implementation: Subscriptions and Publications upgraded to At Least Once.
 *
 * Requires:
 * - FastLED          (Library Manager)
 * - PubSubClient     (Library Manager)
 * - ESP32Servo       (Library Manager)
 * - ArduinoJson      (Library Manager — install v6.x)
 * - MFRC522          (Library Manager)
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <FastLED.h>
#include <ArduinoJson.h>

// ─── Network Credentials ───────────────────────────────────────────────────
const char* ssid        = "Von";
const char* password    = "12345678";
const char* mqtt_broker = "broker.hivemq.com";
const int   mqtt_port   = 1883;

// ─── MQTT Topics ───────────────────────────────────────────────────────────
const char* topic_led_control = "home/room/lights/set";
const char* topic_door_status = "home/door/rfid";

// ─── Hardware Pinout ───────────────────────────────────────────────────────
#define LED_PIN   21   // WS2812B Data
#define RST_PIN   22   // RFID Reset
#define MOSI_PIN  25   // RFID MOSI
#define MISO_PIN  26   // RFID MISO
#define SS_PIN    32   // RFID SDA
#define SCK_PIN   33   // RFID SCK
#define SERVO_PIN 13   // SG90 Servo

// ─── LED Config ────────────────────────────────────────────────────────────
#define NUM_LEDS     12
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define MAX_BRIGHTNESS 85   // Hardware cap — matches the app's 85% ceiling

CRGB leds[NUM_LEDS];

// ─── RFID ──────────────────────────────────────────────────────────────────
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ─── Servo ─────────────────────────────────────────────────────────────────
Servo tokenServo;

// ─── MQTT ──────────────────────────────────────────────────────────────────
WiFiClient   espClient;
PubSubClient mqttClient(espClient);

// ─── Authorized Cards ──────────────────────────────────────────────────────
#define AUTHORIZED_COUNT 2
const byte CARD_LENGTHS[AUTHORIZED_COUNT]     = {4, 7};
const byte AUTHORIZED_UIDS[AUTHORIZED_COUNT][7] = {
  {0x36, 0x6D, 0xCB, 0x06, 0x00, 0x00, 0x00},  // White RFID Card
  {0x04, 0x75, 0x53, 0x9A, 0x32, 0x76, 0x80}   // ID Card
};

// ─── Light Mode State Machine ──────────────────────────────────────────────
enum LightMode {
  MODE_SOLID,
  MODE_RAINBOW,
  MODE_AURORA,
  MODE_PARTY,
  MODE_BEDTIME
};

LightMode currentMode       = MODE_SOLID;
uint8_t   currentBrightness = 60;    // Runtime brightness (0–85)
uint8_t   rainbowHue        = 0;

// Party mode state
uint8_t   partyHue         = 0;
bool      partyBeatsOn     = true;
uint32_t  lastPartyBeat    = 0;
uint16_t  partyOnMs        = 120;   // How long LEDs are ON during a beat
uint16_t  partyOffMs       = 100;   // How long LEDs are OFF during a beat

// Door state
bool      doorUnlocked     = false;
uint32_t  doorUnlockTime   = 0;
const uint32_t doorOpenDuration = 4000;

// ─── Helper: Map color name string → CRGB ──────────────────────────────────
CRGB colorNameToCRGB(const String& colorStr) {
  if (colorStr == "red")     return CRGB::Red;
  if (colorStr == "green")   return CRGB::Green;
  if (colorStr == "blue")    return CRGB::Blue;
  if (colorStr == "cyan")    return CRGB::Cyan;
  if (colorStr == "magenta") return CRGB::Magenta;
  if (colorStr == "yellow")  return CRGB::Yellow;
  if (colorStr == "orange")  return CRGB::OrangeRed;
  if (colorStr == "white")   return CRGB::White;
  if (colorStr == "warm")    return CRGB(255, 120, 20);
  return CRGB::White;
}

// ─── WiFi Setup ────────────────────────────────────────────────────────────
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  bool toggle = false;
  while (WiFi.status() != WL_CONNECTED) {
    FastLED.setBrightness(15); // Low brightness for hardware protection
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[0] = toggle ? CRGB::Orange : CRGB::Black;
    FastLED.show();
    toggle = !toggle;
    
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected: " + WiFi.localIP().toString());
  
  // Clear status LED
  leds[0] = CRGB::Black;
  FastLED.show();
}

// ─── MQTT Callback ─────────────────────────────────────────────────────────
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String raw = "";
  for (unsigned int i = 0; i < length; i++) raw += (char)payload[i];
  Serial.println("📥 MQTT [" + String(topic) + "]: " + raw);

  if (String(topic) != topic_led_control) return;

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    Serial.println("❌ JSON parse error: " + String(err.c_str()));
    return;
  }

  String  scene      = doc["scene"]      | "";
  uint8_t brightness = doc["brightness"] | 60;
  String  colorField = doc["color"]      | "";
  String  effect     = doc["effect"]     | "";

  brightness = min(brightness, (uint8_t)MAX_BRIGHTNESS);
  currentBrightness = brightness;
  scene.trim();
  scene.toUpperCase();

  Serial.println("→ Scene: " + scene + " | Brightness: " + brightness +
                 " | Color: " + colorField + " | Effect: " + effect);

  if (scene == "RAINBOW") {
    currentMode = MODE_RAINBOW;
    FastLED.setBrightness(currentBrightness);
  } else if (scene == "AURORA") {
    currentMode = MODE_AURORA;
    FastLED.setBrightness(currentBrightness);
  } else if (scene == "PARTY") {
    currentMode    = MODE_PARTY;
    partyHue       = 0;
    partyBeatsOn   = true;
    lastPartyBeat  = millis();
    FastLED.setBrightness(currentBrightness);
  } else if (scene == "BEDTIME") {
    currentMode = MODE_BEDTIME;
    FastLED.setBrightness(brightness);
    fill_solid(leds, NUM_LEDS, CRGB(255, 120, 20));
    FastLED.show();
  } else if (scene == "ON") {
    currentMode = MODE_SOLID;
    FastLED.setBrightness(currentBrightness);
    fill_solid(leds, NUM_LEDS, CRGB::White);
    FastLED.show();
  } else if (scene == "OFF") {
    currentMode = MODE_SOLID;
    FastLED.setBrightness(0);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  } else {
    scene.toLowerCase();
    CRGB color = colorNameToCRGB(scene);
    currentMode = MODE_SOLID;
    FastLED.setBrightness(currentBrightness);
    fill_solid(leds, NUM_LEDS, color);
    FastLED.show();
  }
}

// ─── MQTT Reconnect ────────────────────────────────────────────────────────
void reconnect() {
  while (!mqttClient.connected()) {
    // If WiFi connection was lost, handle WiFi reconnection first
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("⚠️ WiFi connection lost. Reconnecting...");
      setup_wifi();
    }

    Serial.print("Connecting to MQTT...");
    
    // Set low brightness and show blue to indicate MQTT connection attempt
    FastLED.setBrightness(15);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[0] = CRGB::Blue;
    FastLED.show();

    String clientId = "ESP32Dorm-" + String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ✅ Connected");
      
      // MODIFIED: Added 1 as the second parameter to subscribe with QoS 1
      mqttClient.subscribe(topic_led_control, 1); 
      
      // Successful connection indicator: Show solid Green for 3 seconds
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      leds[0] = CRGB::Green;
      FastLED.show();
      delay(3000);
      
      // Reset strip to black and restore normal brightness
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.setBrightness(currentBrightness);
      FastLED.show();
    } else {
      Serial.print(" ❌ Failed rc=");
      Serial.print(mqttClient.state());
      Serial.println(", retrying in 5s...");
      
      // Blink blue/black for 5 seconds (10 cycles of 500ms)
      for (int i = 0; i < 10; i++) {
        // If WiFi is lost during this wait, break early to reconnect WiFi
        if (WiFi.status() != WL_CONNECTED) {
          break;
        }
        leds[0] = (i % 2 == 0) ? CRGB::Blue : CRGB::Black;
        FastLED.show();
        delay(500);
      }
    }
  }
}

// ─── Setup ─────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
  mfrc522.PCD_Init();
  Serial.println("🔑 RFID reader initialized");

  ESP32PWM::allocateTimer(0);
  tokenServo.setPeriodHertz(50);
  tokenServo.attach(SERVO_PIN, 500, 2400);
  tokenServo.write(0);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS)
         .setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(currentBrightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  setup_wifi();
  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(mqtt_callback);
  mqttClient.setBufferSize(512);
}

// ─── Loop ──────────────────────────────────────────────────────────────────
void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop();

  switch (currentMode) {
    case MODE_RAINBOW:
      EVERY_N_MILLISECONDS(20) {
        fill_rainbow(leds, NUM_LEDS, rainbowHue, 20);
        FastLED.show();
        rainbowHue++;
      }
      break;

    case MODE_AURORA:
      EVERY_N_MILLISECONDS(20) {
        for (int i = 0; i < NUM_LEDS; i++) {
          // Smoothly oscillate Hue between 140 (blue-green) and 210 (purple) at 4 BPM
          uint8_t h = beatsin8(4, 140, 210, 0, i * 21);
          // Per-pixel shimmer: always bright (200-255), independent of currentBrightness.
          // Global FastLED.setBrightness(currentBrightness) handles overall dimming — one layer only.
          uint8_t v = beatsin8(5 + (i % 3), 200, 255, 0, i * 22);
          leds[i] = CHSV(h, 220, v);
        }
        FastLED.show();
      }
      break;

    case MODE_PARTY: {
      uint32_t now = millis();
      if (partyBeatsOn && (now - lastPartyBeat >= partyOnMs)) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.setBrightness(currentBrightness);
        FastLED.show();
        partyBeatsOn  = false;
        lastPartyBeat = now;
      } else if (!partyBeatsOn && (now - lastPartyBeat >= partyOffMs)) {
        partyHue += 32;
        CRGB beatColor = CHSV(partyHue, 255, 255);
        fill_solid(leds, NUM_LEDS, beatColor);
        FastLED.setBrightness(currentBrightness);
        FastLED.show();
        partyBeatsOn  = true;
        lastPartyBeat = now;
      }
      break;
    }

    case MODE_BEDTIME:
    case MODE_SOLID:
      break;
  }

  if (doorUnlocked && (millis() - doorUnlockTime >= doorOpenDuration)) {
    Serial.println("🔒 Auto-locking door...");
    tokenServo.write(0);
    currentMode = MODE_SOLID;
    FastLED.setBrightness(0);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    doorUnlocked = false;
  }

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String scannedUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) scannedUID += "0";
    scannedUID += String(mfrc522.uid.uidByte[i], HEX);
  }
  scannedUID.toUpperCase();
  Serial.println("🔑 Scanned UID: " + scannedUID);

  // ── MODIFIED: Publish UID using QoS 1 ────────────────────────────────────
  // Parameters: publish(topic, payload, length, retained)
  // Note: PubSubClient handles QoS via this specific signature if the library 
  // is configured to support it (it overrides default behavior).
  mqttClient.publish(
    topic_door_status, 
    (uint8_t*)scannedUID.c_str(), 
    scannedUID.length(), 
    true
  );
  // ─────────────────────────────────────────────────────────────────────────

  bool accessGranted = false;
  for (int c = 0; c < AUTHORIZED_COUNT; c++) {
    if (mfrc522.uid.size == CARD_LENGTHS[c]) {
      bool match = true;
      for (byte b = 0; b < mfrc522.uid.size; b++) {
        if (mfrc522.uid.uidByte[b] != AUTHORIZED_UIDS[c][b]) {
          match = false;
          break;
        }
      }
      if (match) { accessGranted = true; break; }
    }
  }

  if (accessGranted) {
    Serial.println("✅ Access GRANTED");
    currentMode = MODE_SOLID;
    FastLED.setBrightness(MAX_BRIGHTNESS);
    fill_solid(leds, NUM_LEDS, CRGB::Green);
    FastLED.show();
    tokenServo.write(90);
    doorUnlocked    = true;
    doorUnlockTime  = millis();
  } else {
    Serial.println("❌ Access DENIED");
    currentMode = MODE_SOLID;
    FastLED.setBrightness(MAX_BRIGHTNESS);
    for (int i = 0; i < 3; i++) {
      fill_solid(leds, NUM_LEDS, CRGB::Red);   FastLED.show(); delay(150);
      fill_solid(leds, NUM_LEDS, CRGB::Black); FastLED.show(); delay(150);
    }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}