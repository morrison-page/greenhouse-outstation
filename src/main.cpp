#include <Arduino.h>
#include <Wire.h>
#include <DHT20.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define DHT20_SDA 18
#define DHT20_SCL 19
#define LDR_PIN 34

// LED PINs
#define LIGHTS_LED_PIN 16
#define HEATING_LED_PIN 17
#define VENTILATION_LED_PIN 25
#define IRRIGATION_LED_PIN 26

const char* WIFI_SSID = "IoT";
const char* WIFI_PASSWORD = "1nt3rn3t!";

const char* MQTT_BROKER = "192.168.206.92";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USERNAME = "greenhouse";
const char* MQTT_PASSWORD = "greenhouse";

const unsigned long PUBLISH_INTERVAL_MS = 5000;

DHT20 dht20;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

char metricsTopic[96];
char statusTopic[96];
char mqttClientId[48];
unsigned long lastPublishMs = 0;

void buildDeviceIdentifiers() {
    uint64_t chipId = ESP.getEfuseMac();
    uint16_t high = (uint16_t)(chipId >> 32);
    uint32_t low = (uint32_t)chipId;
    char deviceId[20];

    snprintf(deviceId, sizeof(deviceId), "%04X%08X", high, low);
    snprintf(metricsTopic, sizeof(metricsTopic), "devices/%s/metrics", deviceId);
    snprintf(statusTopic, sizeof(statusTopic), "devices/%s/status", deviceId);
    snprintf(mqttClientId, sizeof(mqttClientId), "esp32-%s", deviceId);
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return;
    }

    Serial.print("Connecting to WiFi");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.println(" connected");
}

void connectMqtt() {
    while (!mqttClient.connected()) {
        Serial.print("Connecting to MQTT...");
        if (mqttClient.connect(mqttClientId, MQTT_USERNAME, MQTT_PASSWORD)) {
            Serial.println(" connected");
        } else {
            Serial.print(" failed, state=");
            Serial.print(mqttClient.state());
            Serial.println(" retrying in 5s");
            delay(5000);
        }
    }
}

void setup() {
    Serial.begin(9600);
    Wire.begin(DHT20_SDA, DHT20_SCL);
    dht20.begin();

    pinMode(LIGHTS_LED_PIN, OUTPUT);
    pinMode(HEATING_LED_PIN, OUTPUT);
    pinMode(VENTILATION_LED_PIN, OUTPUT);
    pinMode(IRRIGATION_LED_PIN, OUTPUT);

    buildDeviceIdentifiers();

    WiFi.mode(WIFI_STA);
    connectWiFi();

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    connectMqtt();

    Serial.print("MQTT topics: ");
    Serial.print(metricsTopic);
    Serial.print(" | ");
    Serial.println(statusTopic);
}

void loop() {
    connectWiFi();
    if (!mqttClient.connected()) {
        connectMqtt();
    }
    mqttClient.loop();

    if ((millis() - lastPublishMs) < PUBLISH_INTERVAL_MS) {
        return;
    }
    lastPublishMs = millis();

    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();
    float light = (analogRead(LDR_PIN) / 4095.0) * 100.0;

    bool lightsOn = light <= 20;
    bool heatingOn = temperature <= 23;
    bool ventilationOn = temperature >= 26;
    bool irrigationOn = humidity >= 80;

    digitalWrite(LIGHTS_LED_PIN, lightsOn);
    digitalWrite(HEATING_LED_PIN, heatingOn);
    digitalWrite(VENTILATION_LED_PIN, ventilationOn);
    digitalWrite(IRRIGATION_LED_PIN, irrigationOn);

    char payload[128];
    snprintf(
        payload,
        sizeof(payload),
        "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.1f}",
        temperature,
        humidity,
        light
    );

    if (mqttClient.publish(metricsTopic, payload)) {
        Serial.print("Published to ");
        Serial.print(metricsTopic);
        Serial.print(": ");
        Serial.println(payload);
    } else {
        Serial.println("MQTT publish failed");
    }

    char statusPayload[128];
    snprintf(
    statusPayload,
    sizeof(statusPayload),
        "{\"local_control_setting\":1,\"lights\":%d,\"heating\":%d,\"irrigation\":%d,\"ventilation\":%d}",
        lightsOn ? 1 : 0,
        heatingOn ? 1 : 0,
        irrigationOn ? 1 : 0,
        ventilationOn ? 1 : 0
    );

    if (mqttClient.publish(statusTopic, statusPayload)) {
        Serial.print("Published to ");
        Serial.print(statusTopic);
        Serial.print(": ");
        Serial.println(statusPayload);
    } else {
        Serial.println("MQTT status publish failed");
    }
}