#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// WiFi
const char *ssid = "Nothing2G"; // Enter your WiFi name
const char *password = "samfinknajs";  // Enter WiFi password

// MQTT Broker
const char *mqtt_broker = "192.168.1.10";
const char *topic = "esp8266/test";
const char *mqtt_username = "";
const char *mqtt_password = "";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

void callback(char *topic, byte *payload, unsigned int length);

float roomTemp = 23.1f;
float boilerTemp = 23.1f;

void setup() {
    // Set software serial baud to 115200;
    Serial.begin(9600);
    // connecting to a WiFi network
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.println("Connecting to WiFi..");
    }
    Serial.println("Connected to the WiFi network");
    //connecting to a mqtt broker
    client.setServer(mqtt_broker, mqtt_port);
//    client.setCallback([](char* topic, uint8_t* payload, unsigned int length) {
//        Serial.print("Message arrived in topic: ");
//        Serial.println(topic);
//        Serial.print("Message:");
//        for (int i = 0; i < length; i++) {
//            Serial.print((char) payload[i]);
//        }
//        Serial.println();
//        Serial.println("-----------------------");
//    });
    while (!client.connected()) {
        String client_id = "esp8266-client-";
        client_id += String(WiFi.macAddress());
        Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
        if (client.connect(client_id.c_str())) {
            Serial.println("Public emqx mqtt broker connected");
        } else {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }
//    client.subscribe(topic);
    // publish and subscribe
    client.publish("thermoino/room", String(roomTemp).c_str());
    client.publish("thermoino/boiler", String(boilerTemp).c_str());
}

#define BUFFER_SIZE 40
char buffer[BUFFER_SIZE];
void loop() {
    if (Serial.available() > 0) {
        size_t read = Serial.readBytesUntil('\n', buffer, BUFFER_SIZE);
        buffer[read] = '\0';
        const String &sBuffer = String(buffer);
        if (sBuffer.startsWith("DRQ:")) {
            const String &commandBuffer = sBuffer.substring(4);
            if (commandBuffer.startsWith("RT:")) {
                const String &valueBuffer = commandBuffer.substring(3);
                roomTemp = strtod(valueBuffer.c_str(), nullptr);
                client.publish("thermoino/room", String(roomTemp).c_str());
                Serial.println("Changed room to: " + String(roomTemp));
            } else if (commandBuffer.startsWith("BT:")) {
                const String &valueBuffer = commandBuffer.substring(3);
                boilerTemp = strtod(valueBuffer.c_str(), nullptr);
                client.publish("thermoino/boiler", String(boilerTemp).c_str());
                Serial.println("Changed boiler to: " + String(boilerTemp));
            } else {
                Serial.println("Unknown command: " + sBuffer);
            }
        }
    }
    client.loop();
}