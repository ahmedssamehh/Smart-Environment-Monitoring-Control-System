#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ================= WIFI =================
const char* ssid = "Wokwi-GUEST";   
const char* password = "";

// ================= MQTT =================
const char* mqtt_server = "test.mosquitto.org";
const int mqtt_port = 1883;
const char* prefix = "team_env/";   

WiFiClient espClient;
PubSubClient client(espClient);

// ================= PINS =================
#define DHTPIN 4
#define DHTTYPE DHT22
#define LDR_PIN 34
#define PIR_PIN 27
#define TRIG_PIN 5
#define ECHO_PIN 18

#define RED_LED 2
#define GREEN_LED 15
#define YELLOW_LED 13
#define BUZZER 12
#define SERVO_PIN 14
#define RELAY_PIN 26

DHT dht(DHTPIN, DHTTYPE);
Servo myServo;

// ================= VARIABLES =================
float tempMax = 30.0;
int lightMin = 500;
float distMin = 20.0;

unsigned long lastSensorRead = 0;
unsigned long lastHeartbeat = 0;
const long sensorInterval = 2000;
const long heartbeatInterval = 10000;

bool buzzerActive = false;
unsigned long buzzerStart = 0;

bool manualRed = false;
bool manualGreen = false;
bool manualYellow = false;
bool manualRelay = false;
bool manualServo = false;

int servoAngle = 0;

// ====================================================
// WIFI
// ====================================================
void setupWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
}

// ====================================================
// MQTT RECONNECT
// ====================================================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting MQTT...");
    if (client.connect("ESP32_Client")) {
      Serial.println("Connected");

      client.subscribe((String(prefix) + "actuators/#").c_str());
      client.subscribe((String(prefix) + "config/#").c_str());

    } else {
      Serial.print("Failed, retrying...");
      delay(2000);
    }
  }
}

// ====================================================
// MQTT CALLBACK
// ====================================================
void callback(char* topic, byte* payload, unsigned int length) {

  StaticJsonDocument<256> doc;
  deserializeJson(doc, payload, length);

  String topicStr = String(topic);

  // LED control
  if (topicStr.endsWith("actuators/led")) {
    String color = doc["color"];
    String state = doc["state"];

    if (color == "red") {
      manualRed = true;
      digitalWrite(RED_LED, state == "on");
    }
    if (color == "green") {
      manualGreen = true;
      digitalWrite(GREEN_LED, state == "on");
    }
    if (color == "yellow") {
      manualYellow = true;
      digitalWrite(YELLOW_LED, state == "on");
    }
  }

  // Buzzer
  if (topicStr.endsWith("actuators/buzzer")) {
    digitalWrite(BUZZER, doc["state"] == "on");
  }

  // Servo
  if (topicStr.endsWith("actuators/servo")) {
    servoAngle = doc["angle"];
    manualServo = true;
    myServo.write(servoAngle);
  }

  // Relay
  if (topicStr.endsWith("actuators/relay")) {
    manualRelay = true;
    digitalWrite(RELAY_PIN, doc["state"] == "on");
  }

  // Threshold update
  if (topicStr.endsWith("config/thresholds")) {
    tempMax = doc["temp_max"];
    lightMin = doc["light_min"];
    distMin = doc["dist_min"];
  }
}

// ====================================================
// SENSOR READ
// ====================================================
void readSensors() {

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int light = analogRead(LDR_PIN);
  bool motion = digitalRead(PIR_PIN);

  // Ultrasonic
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2;

  // ================= AUTO RULES =================
  if (!manualRed)
    digitalWrite(RED_LED, temperature > tempMax);

  if (!manualYellow)
    digitalWrite(YELLOW_LED, light < lightMin);

  if (motion && !buzzerActive) {
    digitalWrite(BUZZER, HIGH);
    buzzerActive = true;
    buzzerStart = millis();
  }

if (!manualServo) {
  if (distance < distMin) {
    myServo.write(90);
  } else {
    myServo.write(0);
  }
}

  if (buzzerActive && millis() - buzzerStart > 2000) {
    digitalWrite(BUZZER, LOW);
    buzzerActive = false;
  }

  // ================= MQTT PUBLISH =================

  StaticJsonDocument<128> doc;

  doc["value"] = temperature;
  doc["unit"] = "C";
  publish("sensors/temperature", doc);

  doc.clear();
  doc["value"] = humidity;
  doc["unit"] = "%";
  publish("sensors/humidity", doc);

  doc.clear();
  doc["value"] = light;
  publish("sensors/light", doc);

  doc.clear();
  doc["detected"] = motion;
  publish("sensors/motion", doc);

  doc.clear();
  doc["value"] = distance;
  doc["unit"] = "cm";
  publish("sensors/distance", doc);

  Serial.println("Sensors updated");
}

// ====================================================
void publish(String topic, StaticJsonDocument<128>& doc) {
  char buffer[128];
  serializeJson(doc, buffer);
  client.publish((String(prefix) + topic).c_str(), buffer);
}

// ====================================================
// HEARTBEAT
// ====================================================
void sendHeartbeat() {
  StaticJsonDocument<128> doc;
  doc["uptime"] = millis() / 1000;
  doc["rssi"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();

  publish("system/status", doc);
}

// ====================================================
// SETUP
// ====================================================
void setup() {

  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  dht.begin();
  myServo.attach(SERVO_PIN);

  setupWiFi();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// ====================================================
// LOOP
// ====================================================
void loop() {

  if (!client.connected())
    reconnect();

  client.loop();

  if (millis() - lastSensorRead > sensorInterval) {
    lastSensorRead = millis();
    readSensors();
  }

  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
}