

#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ----------------- WiFi & MQTT Settings -----------------
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASS     = "";
const char* BROKER_ADDR   = "test.mosquitto.org";
const int   BROKER_PORT   = 1883;
const char* CLIENT_ID     = "ESP32_EnvMonitor";

// ----------------- Pin Configurations-----------------
#define PIN_DHT        16
#define PIN_LDR        35
#define PIN_PIR        32
#define PIN_TRIG       25
#define PIN_ECHO       26
#define PIN_LED_RED    14
#define PIN_LED_GREEN  12
#define PIN_LED_YELLOW 27
#define PIN_BUZZER     33
#define PIN_SERVO      23
#define PIN_RELAY      19

// ----------------- Objects -----------------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
DHTesp dhtSensor;
Servo servoBarrier;

// ----------------- Thresholds -----------------
float maxTemperature = 30.0f;  
int   minLight      = 500;    
float minDistance   = 20.0f;  

// ----------------- Timing -----------------
unsigned long lastSensorMs    = 0;
unsigned long lastHeartbeatMs = 0;
const unsigned long SENSOR_INTERVAL_MS    = 2000;
const unsigned long HEARTBEAT_INTERVAL_MS = 10000;

bool buzzerActive      = false;
unsigned long buzzerStartTime = 0;
const unsigned long BUZZER_DURATION_MS  = 2000;

// ----------------- Manual Overrides -----------------
bool overrideLED    = false;
bool overrideBuzzer = false;
bool overrideServo  = false;
bool overrideRelay  = false;

// ----------------- WIFI -----------------
void setupWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.print("Connecting WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println(" failed, retrying...");
  }
}

// ----------------- MQTT Callback -----------------
void onMQTTMessage(char* topic, byte* payload, unsigned int len) {
  char msg[256];
  len = (len >= sizeof(msg)) ? sizeof(msg)-1 : len;
  memcpy(msg, payload, len);
  msg[len] = '\0';

  Serial.print("MQTT received ["); Serial.print(topic); Serial.print("]: "); Serial.println(msg);

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, msg)) return;

  String t = String(topic);

  if (t == "actuators/led") {
    String state = doc["state"] | "off";
    String color = doc["color"] | "red";

    overrideLED = (state == "on");

    digitalWrite(PIN_LED_RED, LOW);
    digitalWrite(PIN_LED_GREEN, LOW);
    digitalWrite(PIN_LED_YELLOW, LOW);

    if (state == "on") {
      if (color == "red")    digitalWrite(PIN_LED_RED, HIGH);
      if (color == "green")  digitalWrite(PIN_LED_GREEN, HIGH);
      if (color == "yellow") digitalWrite(PIN_LED_YELLOW, HIGH);
    }
  }
  else if (t == "actuators/buzzer") {
    String state = doc["state"] | "off";
    overrideBuzzer = (state == "on");
    digitalWrite(PIN_BUZZER, overrideBuzzer ? HIGH : LOW);
  }
  else if (t == "actuators/servo") {
    int angle = doc["angle"] | 0;
    overrideServo = true;
    angle = constrain(angle, 0, 180);
    servoBarrier.write(angle);
  }
  else if (t == "actuators/relay") {
    String state = doc["state"] | "off";
    overrideRelay = (state == "on");
    digitalWrite(PIN_RELAY, overrideRelay ? HIGH : LOW);
  }
  else if (t == "config/thresholds") {
    maxTemperature = doc["temp_max"] | maxTemperature;
    minLight       = doc["light_min"] | minLight;
    minDistance    = doc["dist_min"] | minDistance;

    Serial.printf("Thresholds updated -> Temp: %.1f, Light: %d, Dist: %.1f\n", maxTemperature, minLight, minDistance);
  }
}

// ----------------- MQTT Connect -----------------
void connectMQTT() {
  if (mqttClient.connected()) return;

  while (!mqttClient.connected()) {
    Serial.print("Connecting MQTT...");
    if (mqttClient.connect(CLIENT_ID)) {
      Serial.println(" connected");
      mqttClient.subscribe("actuators/led");
      mqttClient.subscribe("actuators/buzzer");
      mqttClient.subscribe("actuators/servo");
      mqttClient.subscribe("actuators/relay");
      mqttClient.subscribe("config/thresholds");
    } else {
      Serial.printf(" failed, rc=%d\n", mqttClient.state());
      delay(5000);
    }
  }
}

// ----------------- Sensors -----------------
float getDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long dur = pulseIn(PIN_ECHO, HIGH, 30000);
  float dist = (dur * 0.0343f) / 2.0f;
  if (dist <= 0 || dist > 400) return -1.0f;
  return dist;
}

// ----------------- Publish Sensor Data -----------------
void sendSensorData(float temp, float hum, int light, bool motion, float dist) {
  char buf[160];

  snprintf(buf, sizeof(buf), "{\"value\":%.1f,\"unit\":\"C\"}", temp);
  mqttClient.publish("sensors/temperature", buf);

  snprintf(buf, sizeof(buf), "{\"value\":%.1f,\"unit\":\"%%\"}", hum);
  mqttClient.publish("sensors/humidity", buf);

  const char* lightLevel = (light > minLight) ? "bright" : "dark";
  snprintf(buf, sizeof(buf), "{\"value\":%d,\"level\":\"%s\"}", light, lightLevel);
  mqttClient.publish("sensors/light", buf);

  snprintf(buf, sizeof(buf), "{\"detected\":%s}", motion ? "true" : "false");
  mqttClient.publish("sensors/motion", buf);

  if (dist > 0.0f) {
    snprintf(buf, sizeof(buf), "{\"value\":%.1f,\"unit\":\"cm\"}", dist);
    mqttClient.publish("sensors/distance", buf);
  }
}

// ----------------- Heartbeat -----------------
void sendHeartbeat() {
  char buf[200];
  snprintf(buf, sizeof(buf),
           "{\"uptime\":%lu,\"rssi\":%ld,\"heap\":%u}",
           millis()/1000, WiFi.RSSI(), ESP.getFreeHeap());
  mqttClient.publish("system/status", buf);
}

// ----------------- Automatic Rules -----------------
void runActuatorLogic(float temp, int light, bool motion, float dist) {
  if (!overrideLED && !overrideRelay) {
    if (temp > maxTemperature) {
      digitalWrite(PIN_LED_RED, HIGH);
      digitalWrite(PIN_RELAY, HIGH);
    } else {
      digitalWrite(PIN_LED_RED, LOW);
      digitalWrite(PIN_RELAY, LOW);
    }
  }

  if (!overrideLED) {
    digitalWrite(PIN_LED_YELLOW, light < minLight ? HIGH : LOW);
  }

  if (!overrideBuzzer) {
    if (motion && !buzzerActive) {
      buzzerActive = true;
      buzzerStartTime = millis();
      digitalWrite(PIN_BUZZER, HIGH);
    }
    if (buzzerActive && (millis() - buzzerStartTime >= BUZZER_DURATION_MS)) {
      buzzerActive = false;
      digitalWrite(PIN_BUZZER, LOW);
    }
  }

  if (!overrideServo) {
    servoBarrier.write((dist>0 && dist < minDistance) ? 90 : 0);
  }

  if (!overrideLED) {
    bool normal = (temp <= maxTemperature) && (light >= minLight) && !motion;
    digitalWrite(PIN_LED_GREEN, normal ? HIGH : LOW);
  }
}

// ----------------- Setup -----------------
void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 Environment Monitor ===");

  dhtSensor.setup(PIN_DHT, DHTesp::DHT22);
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_RELAY, OUTPUT);
  servoBarrier.attach(PIN_SERVO);
  servoBarrier.write(0);

  setupWiFi();
  mqttClient.setServer(BROKER_ADDR, BROKER_PORT);
  mqttClient.setCallback(onMQTTMessage);
  mqttClient.setBufferSize(512);
  connectMQTT();
}

// ----------------- Main Loop -----------------
void loop() {
  if (WiFi.status() != WL_CONNECTED) setupWiFi();
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  unsigned long now = millis();
  if (now - lastSensorMs >= SENSOR_INTERVAL_MS) {
    lastSensorMs = now;

    float t = dhtSensor.getTemperature();
    float h = dhtSensor.getHumidity();
    int l = analogRead(PIN_LDR);
    bool m = digitalRead(PIN_PIR) == HIGH;
    float d = getDistance();

    if (!isnan(t) && !isnan(h)) {
      sendSensorData(t, h, l, m, d);
      runActuatorLogic(t, l, m, d);

      Serial.printf("[%lus] T=%.1fC H=%.1f%% L=%d M=%s D=%.1fcm\n",
                    now/1000, t, h, l, m?"Y":"N", d);
    } else {
      Serial.println("DHT read error!");
    }
  }

  if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeatMs = now;
    sendHeartbeat();
  }
}
