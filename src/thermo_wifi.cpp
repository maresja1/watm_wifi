#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

extern "C" {
    #include "user_interface.h"
}

#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

// WiFi
const char *ssid = "Nothing2G"; // Enter your WiFi name
const char *password = "samfinknajs";  // Enter WiFi password

// MQTT Broker
const char *mqtt_username = "";
const char *mqtt_password = "";

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_broker[40];
char mqtt_port[6] = "8080";
char mqtt_api_token[34] = "YOUR_API_TOKEN";

void callback(char *topic, byte *payload, unsigned int length);

float roomTemp = 23.1f;
float boilerTemp = 23.1f;
int angle = 0;
bool circuitRelay = false;
bool heatNeeded = false;

void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

void setup() {
    // Set software serial baud to 115200;
    Serial.begin(115200);
    for (int i = 0; i < 10 && !Serial; ++i) {
        // wait for serial port to connect. Needed for native USB port only
        delay(10);
    }

    //read configuration from FS json
    Serial.println("mounting FS...");

    if (LittleFS.begin()) {
        Serial.println("mounted file system");
        if (LittleFS.exists("/config.json")) {
            //file exists, reading and loading
            Serial.println("reading config file");
            File configFile = LittleFS.open("/config.json", "r");
            if (configFile) {
                Serial.println("opened config file");
                size_t size = configFile.size();
                // Allocate a buffer to store contents of the file.
                std::unique_ptr<char[]> buf(new char[size]);

                configFile.readBytes(buf.get(), size);

                DynamicJsonDocument json(1024);
                auto deserializeError = deserializeJson(json, buf.get());
                serializeJson(json, Serial);
                if ( ! deserializeError ) {
                    Serial.println("\nparsed json");
                    strcpy(mqtt_broker, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(mqtt_api_token, json["api_token"]);
                } else {
                    Serial.println("failed to load json config");
                }
                configFile.close();
            }
        }
    } else {
        Serial.println("failed to mount FS");
    }
    //end read

    // The extra parameters to be configured (can be either global or just in the setup)
    // After connecting, parameter.getValue() will get you the configured value
    // id/name placeholder/prompt default length
    WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_broker, 40);
    WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
    WiFiManagerParameter custom_api_token("apikey", "API token", mqtt_api_token, 32);

    //connecting to a mqtt broker
    WiFiManager wifiManager;

    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    wifiManager.addParameter(&custom_mqtt_server);
    wifiManager.addParameter(&custom_mqtt_port);
    wifiManager.addParameter(&custom_api_token);

    //fetches ssid and pass and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    uint8 mode = 0;
    wifi_softap_dhcps_start();
    if(!wifiManager.autoConnect()) {
        Serial.println("failed to connect and hit timeout");
        //reset and try again, or maybe put it to deep sleep
        ESP.reset();
        delay(1000);
    }
    wifi_softap_dhcps_stop();

    //read updated parameters
    strcpy(mqtt_broker, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_api_token, custom_api_token.getValue());
    Serial.println("The values in the file are: ");
    Serial.println("\tmqtt_broker : " + String(mqtt_broker));
    Serial.println("\tmqtt_port : " + String(mqtt_port));
    Serial.println("\tmqtt_api_token : " + String(mqtt_api_token));

    //save the custom parameters to FS
    if (true) {
        Serial.println("saving config");
        DynamicJsonDocument json(1024);
        json["mqtt_server"] = mqtt_broker;
        json["mqtt_port"] = mqtt_port;
        json["api_token"] = mqtt_api_token;

        File configFile = LittleFS.open("/config.json", "w");
        if (!configFile) {
            Serial.println("failed to open config file for writing");
        }
        serializeJson(json, Serial);
        serializeJson(json, configFile);
        configFile.flush();
        configFile.close();
    }
    client.setServer(mqtt_broker, atoi(mqtt_port));
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
        Serial.printf("The client %s connects to the public mqtt broker\r\n", client_id.c_str());
        if (client.connect(client_id.c_str(), "thermoino", mqtt_api_token)) {
            Serial.println("Public emqx mqtt broker connected");
        } else {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }
//    client.subscribe(topic);
    // publish and subscribe
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
            } else if (commandBuffer.startsWith("O:")) {
                const String &valueBuffer = commandBuffer.substring(3);
                angle = atoi(valueBuffer.c_str());
                client.publish("thermoino/angle", String(angle).c_str());
                Serial.println("Changed angle to: " + String(boilerTemp));
            } else if (commandBuffer.startsWith("R:")) {
                const String &valueBuffer = commandBuffer.substring(3);
                circuitRelay = valueBuffer.equals("true");
                client.publish("thermoino/relay", String(circuitRelay).c_str());
                Serial.println("Changed circuitRelay to: " + String(circuitRelay));
            } else if (commandBuffer.startsWith("HN:")) {
                const String &valueBuffer = commandBuffer.substring(3);
                heatNeeded = valueBuffer.equals("true");
                client.publish("thermoino/heatNeeded", String(heatNeeded).c_str());
                Serial.println("Changed circuitRelay to: " + String(heatNeeded));
            } else {
                Serial.println("Unknown command: " + sBuffer);
            }
        }
    }
    client.loop();
}