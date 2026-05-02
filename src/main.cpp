#include <Arduino.h>
#include <Wire.h>
#include <DHT20.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Pin Definitions
#define DHT20_SDA 18
#define DHT20_SCL 19
#define LDR_PIN 34
#define LIGHTS_LED_PIN 16
#define HEATING_LED_PIN 17
#define VENTILATION_LED_PIN 25
#define IRRIGATION_LED_PIN 26

// Wi-Fi & MQTT Setup
const char* WIFI_SSID = "IoT";
const char* WIFI_PASSWORD = "1nt3rn3t!";
const char* MQTT_BROKER = "192.168.206.92";
const uint16_t MQTT_PORT = 1883;
const char* MQTT_USERNAME = "greenhouse";
const char* MQTT_PASSWORD = "greenhouse";

const unsigned long PUBLISH_INTERVAL_MS = 5000;
unsigned long lastPublishMs = 0;

enum Mode { LOCAL, REMOTE };
Mode mode = LOCAL;

// Available Controls & Pin Mappings
#define NUM_ITEMS 4
const char* items[NUM_ITEMS] = {
    "lights",
    "heating",
    "ventilation",
    "irrigation"
};
const int pins[NUM_ITEMS] = {
    LIGHTS_LED_PIN,
    HEATING_LED_PIN,
    VENTILATION_LED_PIN,
    IRRIGATION_LED_PIN
};
bool states[NUM_ITEMS] = {false, false, false, false};

DHT20 dht20;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Topic buffers
char baseTopic[64];
char metricsTopic[96];
char statusTopic[96];
char modeSetTopic[96];
char modeStateTopic[96];
char ctrlSetTopics[NUM_ITEMS][96];
char ctrlStateTopics[NUM_ITEMS][96];
char mqttClientId[48];

// Device Identification
void buildDeviceIdentifiers() {
    uint64_t chipId = ESP.getEfuseMac();
    uint16_t high = (uint16_t)(chipId >> 32);
    uint32_t low = (uint32_t)chipId;

    char deviceId[20];
    snprintf(deviceId, sizeof(deviceId), "%04X%08X", high, low);

    snprintf(baseTopic, sizeof(baseTopic), "devices/%s", deviceId);
    snprintf(metricsTopic, sizeof(metricsTopic), "%s/metrics", baseTopic);
    snprintf(statusTopic, sizeof(statusTopic), "%s/status", baseTopic);

    snprintf(modeSetTopic, sizeof(modeSetTopic), "%s/mode/set", baseTopic);
    snprintf(modeStateTopic, sizeof(modeStateTopic), "%s/mode/state", baseTopic);

    for (int i = 0; i < NUM_ITEMS; i++) {
        snprintf(ctrlSetTopics[i], sizeof(ctrlSetTopics[i]),
                 "%s/control/%s/set", baseTopic, items[i]);

        snprintf(ctrlStateTopics[i], sizeof(ctrlStateTopics[i]),
                 "%s/control/%s/state", baseTopic, items[i]);
    }

    snprintf(mqttClientId, sizeof(mqttClientId), "esp32-%s", deviceId);
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print("[WiFi] Connecting");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }

    Serial.println("\n[WiFi] Connected");
}

// Control Output Function
void applyOutput(int index, bool on) {
    digitalWrite(pins[index], on ? HIGH : LOW);
    states[index] = on;

    Serial.print("[CONTROL] ");
    Serial.print(items[index]);
    Serial.print(" -> ");
    Serial.println(on ? "ON" : "OFF");

    bool ok = mqttClient.publish(ctrlStateTopics[index],
                                 on ? "ON" : "OFF",
                                 true);

    Serial.print("[MQTT] Publish state ");
    Serial.print(ctrlStateTopics[index]);
    Serial.print(" = ");
    Serial.println(ok ? "OK" : "FAIL");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {

    char msg[16];
    if (length >= sizeof(msg)) return;

    memcpy(msg, payload, length);
    msg[length] = '\0';

    Serial.print("[MQTT RX] ");
    Serial.print(topic);
    Serial.print(" => ");
    Serial.println(msg);

    // Mode Switching
    if (strcmp(topic, modeSetTopic) == 0) {

        if (strcmp(msg, "REMOTE") == 0) mode = REMOTE;
        else if (strcmp(msg, "LOCAL") == 0) mode = LOCAL;

        Serial.print("[MODE] Switched to ");
        Serial.println(mode == LOCAL ? "LOCAL" : "REMOTE");

        mqttClient.publish(modeStateTopic, msg, true);
        return;
    }

    // Control Handling
    if (mode == REMOTE) {
        for (int i = 0; i < NUM_ITEMS; i++) {
            if (strcmp(topic, ctrlSetTopics[i]) == 0) {

                Serial.print("[REMOTE CONTROL] ");
                Serial.print(items[i]);
                Serial.print(" -> ");
                Serial.println(msg);

                if (strcmp(msg, "ON") == 0) {
                    applyOutput(i, true);
                } else if (strcmp(msg, "OFF") == 0) {
                    applyOutput(i, false);
                }
                break;
            }
        }
    } else {
        Serial.println("[IGNORED] Control received in LOCAL mode");
    }
}

void connectMqtt() {
    while (!mqttClient.connected()) {
        Serial.print("[MQTT] Connecting... ");
        if (mqttClient.connect(mqttClientId, MQTT_USERNAME, MQTT_PASSWORD, statusTopic, 1, true, "OFFLINE")) {
            Serial.println("OK");
            mqttClient.publish(statusTopic, "ONLINE", true);
            Serial.println("[MQTT] Published ONLINE status");
            Serial.print("[MQTT] Subscribing: ");
            Serial.println(modeSetTopic);
            mqttClient.subscribe(modeSetTopic);
            for (int i = 0; i < NUM_ITEMS; i++) {
                Serial.print("[MQTT] Subscribing: ");
                Serial.println(ctrlSetTopics[i]);
                mqttClient.subscribe(ctrlSetTopics[i]);
            }
        } else {
            Serial.print("[MQTT] Failed, state=");
            Serial.println(mqttClient.state());
            delay(5000);
        }
    }
}

// Local Automation Logic
void runLocalAutomation(float temperature, float humidity, float light) {
    bool lightsOn = light <= 20;
    bool heatingOn = temperature <= 23;
    bool ventilationOn = temperature >= 26;
    bool irrigationOn = humidity >= 80;

    Serial.println("[LOCAL MODE] Applying automation");

    applyOutput(0, lightsOn);
    applyOutput(1, heatingOn);
    applyOutput(2, ventilationOn);
    applyOutput(3, irrigationOn);
}


void setup() {
    Serial.begin(9600);
    delay(1000);

    Serial.println("\n[BOOT] Starting device...");

    Wire.begin(DHT20_SDA, DHT20_SCL);
    dht20.begin();

    for (int i = 0; i < NUM_ITEMS; i++) {
        pinMode(pins[i], OUTPUT);
        digitalWrite(pins[i], LOW);
    }

    buildDeviceIdentifiers();

    WiFi.mode(WIFI_STA);
    connectWiFi();

    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);

    connectMqtt();

    Serial.print("[INIT] Base topic: ");
    Serial.println(baseTopic);
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

    // Retrieve sensor data
    dht20.read();
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();
    float light = (analogRead(LDR_PIN) / 4095.0f) * 100.0f;

    Serial.println("\n[SENSOR]");
    Serial.print("Temperature: "); Serial.println(temperature);
    Serial.print("Humidity: "); Serial.println(humidity);
    Serial.print("Light: "); Serial.println(light);

    if (mode == LOCAL) {
        runLocalAutomation(temperature, humidity, light);
    }

    // Publish Metrics
    char payload[128];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%.1f}",
             temperature, humidity, light);

    bool ok1 = mqttClient.publish(metricsTopic, payload);

    Serial.print("[MQTT TX] ");
    Serial.print(metricsTopic);
    Serial.print(" => ");
    Serial.println(payload);
    Serial.println(ok1 ? "[MQTT] Metrics sent OK" : "[MQTT] Metrics send FAIL");

    // Publish Control Status
    char statusPayload[192];
    snprintf(statusPayload, sizeof(statusPayload),
             "{\"mode\":\"%s\",\"lights\":%d,\"heating\":%d,\"ventilation\":%d,\"irrigation\":%d}",
             mode == LOCAL ? "LOCAL" : "REMOTE",
             states[0], states[1], states[2], states[3]);

    bool ok2 = mqttClient.publish(statusTopic, statusPayload);

    Serial.print("[MQTT TX] ");
    Serial.print(statusTopic);
    Serial.print(" => ");
    Serial.println(statusPayload);
    Serial.println(ok2 ? "[MQTT] Status sent OK" : "[MQTT] Status send FAIL");
}