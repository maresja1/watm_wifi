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

void callback(char *topic, uint8_t* payload, unsigned int length);

void sendState();

float roomTemp = 23.1f;
float boilerTemp = 23.1f;
uint16_t boilerRefTemp = 70;
int angle = 0;
bool circuitRelay = false;
bool heatNeeded = false;
bool heatOverride = false;
float heatNeededPWM = 0.0f;
float boilerPIDKp = 0.0f;
float boilerPIDKi = 0.0f;
float boilerPIDKd = 0.0f;
float relayPIDKp = 0.0f;
float relayPIDKi = 0.0f;
float relayPIDKd = 0.0f;

void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
}


const String &topicBase = String("/thermoino_") + ESP.getChipId();
const String &generalTopicBase = "homeassistant/climate" + topicBase;
const String &heatNeededSetTopic = String("/heatNeeded/set");
const String &boilerRefTempSetTopic = String("/boilerRefTemp/set");
const String &ventOpenSetTopic = String("/ventOpen/set");
const String &buttonToAutomaticSetTopic = String("/buttonToAutomatic/set");
const String &boilerPIDKpTopic = String("/boilerPIDKp/set");
const String &boilerPIDKiTopic = String("/boilerPIDKi/set");
const String &boilerPIDKdTopic = String("/boilerPIDKd/set");
const String &relayPIDKpTopic = String("/relayPIDKp/set");
const String &relayPIDKiTopic = String("/relayPIDKi/set");
const String &relayPIDKdTopic = String("/relayPIDKd/set");

const uint8_t serialLineBufferCapacity = 40;
uint8_t serialLineBufferIdx = 0;
char serialLineBuffer[serialLineBufferCapacity];   // an array to store the received data
boolean serialLineBufferDataReady = false;

void recvWithEndMarker() {
    if (Serial.available() > 0 && !serialLineBufferDataReady) {
        const char rc = (char) Serial.read();
        if (rc != '\r' && rc != '\n') {
            serialLineBuffer[serialLineBufferIdx++] = rc;
            if (serialLineBufferIdx >= serialLineBufferCapacity) {
                serialLineBufferIdx = 0; // overflow
            }
        } else {
            serialLineBuffer[serialLineBufferIdx] = '\0'; // terminate the string
            serialLineBufferIdx = 0;
            serialLineBufferDataReady = true;
        }
    }
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
    DynamicJsonDocument json(1024);
    if (true) {
        Serial.println("saving config");
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
    client.setCallback(callback);
    while (!client.connected()) {
        const String client_id("esp8266-client-" + String(ESP.getChipId()));
        Serial.printf("The client %s connects to the public mqtt broker\r\n", client_id.c_str());
        if (client.connect(client_id.c_str(), "thermoino", mqtt_api_token)) {
            Serial.println("Public emqx mqtt broker connected");
        } else {
            Serial.print("failed with state ");
            Serial.print(client.state());
            delay(2000);
        }
    }
    json.clear();
    const JsonObject &device = json.createNestedObject("device");
    device["identifiers"] = String(ESP.getChipId());
    device["name"] = "Thermoino";
    json["~"] = generalTopicBase;
    json["stat_t"] = "~/state";

    json["name"] = "Thermoino Room Temp";
    json["device_class"] = "temperature";
    json["unit_of_measurement"] = "°C";
    json["value_template"] = "{{ value_json.roomTemp }}";
    json["unique_id"] = topicBase.substring(1) + "-roomTemp";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-roomTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Boiler Temp";
    json["device_class"] = "temperature";
    json["unit_of_measurement"] = "°C";
    json["value_template"] = "{{ value_json.boilerTemp }}";
    json["unique_id"] = topicBase.substring(1) + "-boilerTemp";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-boilerTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.remove("device_class");
    json.remove("unit_of_measurement");

    json["name"] = "Thermoino Heat demand PWM";
    json["value_template"] = "{{ value_json.heatNeededPWM }}";
    json["unique_id"] = topicBase.substring(1) + "-heatNeededPWM";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-heatNeededPWM/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Angle";
    json["device_class"] = "power_factor";
    json["unit_of_measurement"] = "%";
    json["value_template"] = "{{ value_json.angle }}";
    json["unique_id"] = topicBase.substring(1) + "-angle";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-angle/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.remove("device_class");
    json.remove("unit_of_measurement");

    json["name"] = "Thermoino Circuit Relay";
    json["value_template"] = "{{ value_json.circuitRelay }}";
    json["unique_id"] = topicBase.substring(1) + "-circuitRelay";
    client.beginPublish(("homeassistant/binary_sensor" + topicBase + "-circuitRelay/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Heat demand switch";
    json["value_template"] = "{{ value_json.heatNeeded }}";
    json["unique_id"] = topicBase.substring(1) + "-heatNeeded";
    json["command_topic"] = "~" + heatNeededSetTopic;
    client.beginPublish(("homeassistant/switch" + topicBase + "-heatNeeded/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Boiler ref. temp.";
    json["value_template"] = "{{ value_json.boilerRefTemp }}";
    json["unit_of_measurement"] = "°C";
    json["unique_id"] = topicBase.substring(1) + "-boilerRefTemp";
    json["command_topic"] = "~" + boilerRefTempSetTopic;
    json["min"] = 45.0f;
    json["max"] = 90.0f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerRefTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Vent open set";
    json["value_template"] = "{{ value_json.angle }}";
    json["unit_of_measurement"] = "%";
    json["unique_id"] = topicBase.substring(1) + "-ventOpenSet";
    json["command_topic"] = "~" + ventOpenSetTopic;
    json["min"] = 0.0f;
    json["max"] = 99.0f;
    client.beginPublish(("homeassistant/number" + topicBase + "-ventOpenSet/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.remove("min");
    json.remove("max");
    json.remove("unit_of_measurement");
    json.remove("stat_t");
    json.remove("value_template");

    json["name"] = "Thermoino Automatic button";
    json["unique_id"] = topicBase.substring(1) + "-toAutomatic";
    json["command_topic"] = "~" + buttonToAutomaticSetTopic;
    client.beginPublish(("homeassistant/button" + topicBase + "-toAutomatic/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

//    float boilerPIDKp = 0.0f;
//    float boilerPIDKi = 0.0f;
//    float boilerPIDKd = 0.0f;
//    float relayPIDKp = 0.0f;
//    float relayPIDKi = 0.0f;
//    float relayPIDKd = 0.0f;

    json["name"] = "Thermoino Boiler PID Kp";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKp";
    json["command_topic"] = "~" + boilerPIDKpTopic;
    json["value_template"] = "{{ value_json.boilerPIDKp }}";
    json["stat_t"] = "~/state";
    json["min"] = 0.0f;
    json["max"] = 99.0f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Boiler PID Ki";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKi";
    json["command_topic"] = "~" + boilerPIDKiTopic;
    json["value_template"] = "{{ value_json.boilerPIDKi }}";
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKi/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Boiler PID Kd";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKd";
    json["command_topic"] = "~" + boilerPIDKdTopic;
    json["value_template"] = "{{ value_json.boilerPIDKd }}";
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKd/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Relay PID Kp";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKp";
    json["command_topic"] = "~" + relayPIDKpTopic;
    json["value_template"] = "{{ value_json.relayPIDKp }}";
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Relay PID Ki";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKi";
    json["command_topic"] = "~" + relayPIDKiTopic;
    json["value_template"] = "{{ value_json.relayPIDKi }}";
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKi/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json["name"] = "Thermoino Relay PID Kd";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKd";
    json["command_topic"] = "~" + relayPIDKdTopic;
    json["value_template"] = "{{ value_json.relayPIDKd }}";
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKd/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.remove("min");
    json.remove("max");

    Serial.println("State topic: " + generalTopicBase + "/state");
    // publish and subscribe
    client.subscribe((generalTopicBase + heatNeededSetTopic).c_str());
    client.subscribe((generalTopicBase + boilerRefTempSetTopic).c_str());
    client.subscribe((generalTopicBase + buttonToAutomaticSetTopic).c_str());
    client.subscribe((generalTopicBase + ventOpenSetTopic).c_str());

    client.subscribe((generalTopicBase + boilerPIDKpTopic).c_str());
    client.subscribe((generalTopicBase + boilerPIDKiTopic).c_str());
    client.subscribe((generalTopicBase + boilerPIDKdTopic).c_str());

    client.subscribe((generalTopicBase + relayPIDKpTopic).c_str());
    client.subscribe((generalTopicBase + relayPIDKiTopic).c_str());
    client.subscribe((generalTopicBase + relayPIDKdTopic).c_str());
}

#define literal_len(x) (sizeof(x) - 1)
#define PARSE(x) if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));
#define OR_PARSE(x) } else if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));

#define BUFFER_SIZE 40
char buffer[BUFFER_SIZE + 1];
DynamicJsonDocument doc(1024);
void loop() {
    recvWithEndMarker();
    if (serialLineBufferDataReady) {
        serialLineBufferDataReady = false;
        const String &sBuffer = String(serialLineBuffer);
        if (sBuffer.startsWith("DRQ:")) {
            const String &commandBuffer = sBuffer.substring(4);
            PARSE("RT")
                roomTemp = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("BT")
                boilerTemp = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("BRT")
                boilerRefTemp = atoi(valueBuffer.c_str());
            OR_PARSE("O")
                angle = atoi(valueBuffer.c_str());
            OR_PARSE("R")
                circuitRelay = atoi(valueBuffer.c_str()) != 0;
            OR_PARSE("HN")
                heatNeeded = valueBuffer.equals("true");
            OR_PARSE("HPWM")
                heatNeededPWM = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Kp")
                boilerPIDKp = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Ki")
                boilerPIDKi = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Kd")
                boilerPIDKd = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Kp")
                relayPIDKp = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Ki")
                relayPIDKi = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Kd")
                relayPIDKd = strtod(valueBuffer.c_str(), nullptr);
            } else {
//                Serial.println("Unknown command: " + sBuffer);
            }

            sendState();
        }
    }
    client.loop();
}

void sendState() {
    doc.clear();
    doc["roomTemp"] = roomTemp;
    doc["boilerTemp"] = boilerTemp;
    doc["angle"] = angle;
    doc["circuitRelay"] = circuitRelay ? "ON" : "OFF";
    doc["heatNeeded"] = heatNeeded ? "ON" : "OFF";
    doc["heatNeededPWM"] = heatNeededPWM;
    doc["boilerRefTemp"] = boilerRefTemp;

    doc["boilerPIDKp"] = boilerPIDKp;
    doc["boilerPIDKi"] = boilerPIDKi;
    doc["boilerPIDKd"] = boilerPIDKd;
    doc["relayPIDKp"] = relayPIDKp;
    doc["relayPIDKi"] = relayPIDKi;
    doc["relayPIDKd"] = relayPIDKd;

    const String &topicBaseState = "homeassistant/climate" + topicBase + "/state";
//    Serial.println(topicBaseState);
    client.beginPublish(topicBaseState.c_str(), measureJson(doc), true);
    serializeJson(doc, client);
    client.endPublish();
//    serializeJson(doc, Serial);
//    Serial.println();
}


void callback(char* topic, uint8_t* payload, unsigned int length) {
    String topicStr = String(topic);

    if (!topicStr.startsWith(generalTopicBase)) return;

//    Serial.print("Message arrived [");
//    Serial.print(topic);
//    Serial.print("] ");
    char payloadCStr[length + 1];
    for (int i = 0; i < length; i++) {
        payloadCStr[i] = (char)payload[i];
    }
    payloadCStr[length] = '\0';
//    Serial.print(payloadCStr);
//    Serial.println();

    if (topicStr.equals(generalTopicBase + heatNeededSetTopic)) {
        const bool lastHeatNeeded = heatNeeded;
        heatNeeded = strcasecmp(payloadCStr, "on") == 0;
        heatOverride = true;
        if (lastHeatNeeded != heatNeeded) {
            Serial.print("DRQ:HNO:");
            Serial.println(heatOverride ? (heatNeeded ? "2" : "1") : "0");
        }
    }

    if (topicStr.equals(generalTopicBase + boilerRefTempSetTopic)) {
        uint16_t boilerRefTempLocal = atoi(payloadCStr);
        if (boilerRefTempLocal != boilerRefTemp) {
            Serial.print("DRQ:BRT:");
            Serial.println(boilerRefTempLocal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKpTopic)) {
        double boilerPIDKpVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKpVal != boilerRefTemp) {
            Serial.print("DRQ:PID_BL_Kp:");
            Serial.println(boilerPIDKpVal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKiTopic)) {
        double boilerPIDKiVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKiVal != boilerRefTemp) {
            Serial.print("DRQ:PID_BL_Ki:");
            Serial.println(boilerPIDKiVal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKdTopic)) {
        double boilerPIDKdVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKdVal != boilerRefTemp) {
            Serial.print("DRQ:PID_BL_Kd:");
            Serial.println(boilerPIDKdVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKpTopic)) {
        double relayPIDKpVal = strtod(payloadCStr, nullptr);
        if (relayPIDKpVal != boilerRefTemp) {
            Serial.print("DRQ:PID_CR_Kp:");
            Serial.println(relayPIDKpVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKiTopic)) {
        double relayPIDKiVal = strtod(payloadCStr, nullptr);
        if (relayPIDKiVal != boilerRefTemp) {
            Serial.print("DRQ:PID_CR_Ki:");
            Serial.println(relayPIDKiVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKdTopic)) {
        double relayPIDKdVal = strtod(payloadCStr, nullptr);
        if (relayPIDKdVal != boilerRefTemp) {
            Serial.print("DRQ:PID_CR_Kd:");
            Serial.println(relayPIDKdVal);
        }
    }

    if (topicStr.equals(generalTopicBase + ventOpenSetTopic)) {
        const int32_t ventOpen = atoi(payloadCStr);
        Serial.print("DRQ:O:");
        Serial.println(ventOpen);
    }

    if (topicStr.equals(generalTopicBase + buttonToAutomaticSetTopic)) {
        const bool pressed = strcasecmp(payloadCStr, "press") == 0;
        if (pressed) {
            Serial.println("DRQ:M:A");
            sendState();
        }
    }
}