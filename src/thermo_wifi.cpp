#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>

extern "C" {
    #include "user_interface.h"
}

#include <PubSubClient.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include "thermo_wifi.h"

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_broker[40] = "unset";
char mqtt_port[6] = "8080";
char mqtt_api_token[34] = "YOUR_API_TOKEN";
const uint32_t BUFFER_MAX_SIZE = 8*1024;
char buffer[BUFFER_MAX_SIZE] = "";
DynamicJsonDocument json(1024);

// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
WiFiManagerParameter *custom_mqtt_server;
WiFiManagerParameter *custom_mqtt_port;
WiFiManagerParameter *custom_api_token;


float roomTemp = 0.0f;
float roomRefTemp = 0.0f;
float boilerTemp = 0.0f;
uint16_t boilerRefTemp = 0;
uint16_t angle = 0;
bool circuitRelay = false;
bool heatNeeded = false;
float heatNeededPWM = -100.0f; // -100 to detect init data received
float boilerPIDKp = 0.0f;
float boilerPIDKi = 0.0f;
float boilerPIDKd = 0.0f;
float relayPIDKp = 0.0f;
float relayPIDKi = 0.0f;
float relayPIDKd = 0.0f;

const String &topicBase = String("/thermoino_") + EspClass::getChipId();
const String &generalTopicBase = "homeassistant/climate" + topicBase;
const String &heatNeededSetTopic = String("/heatNeeded/set");
const String &boilerRefTempSetTopic = String("/boilerRefTemp/set");
const String &roomRefTempSetTopic = String("/roomRefTemp/set");
const String &ventOpenSetTopic = String("/ventOpen/set");
const String &buttonToAutomaticSetTopic = String("/buttonToAutomatic/set");
const String &boilerPIDKpTopic = String("/boilerPIDKp/set");
const String &boilerPIDKiTopic = String("/boilerPIDKi/set");
const String &boilerPIDKdTopic = String("/boilerPIDKd/set");
const String &relayPIDKpTopic = String("/relayPIDKp/set");
const String &relayPIDKiTopic = String("/relayPIDKi/set");
const String &relayPIDKdTopic = String("/relayPIDKd/set");

const uint16_t serialLineBufferCapacity = 1024;
uint16_t serialLineBufferIdx = 0;
char serialLineBuffer[serialLineBufferCapacity + 1] = "";   // an array to store the received data
uint8_t serialLineBufferDataReady = 0x0;

void recvWithEndMarker() {
    while(Serial.available() > 0 && !(serialLineBufferDataReady & 0x1)) {
        int rc = Serial.read();
        if (rc < 0) {
            break;
        } else if (rc != '\r' && rc != '\n') {
            serialLineBuffer[serialLineBufferIdx++] = (char)rc;
            if (serialLineBufferIdx >= serialLineBufferCapacity) {
                serialLineBufferIdx = 0; // overflow
                serialLineBuffer[serialLineBufferCapacity] = 0x0;
                serialLineBufferDataReady |= 0x2;
            }
        } else {
            if (serialLineBufferIdx == 0) {
                // ignore empty lines
                continue;
            }
            serialLineBuffer[serialLineBufferIdx] = '\0'; // terminate the string
            serialLineBufferIdx = 0;
            serialLineBufferDataReady |= 0x1;
        }
    }
}

void saveConfig()
{
    //read updated parameters
    strcpy(mqtt_broker, custom_mqtt_server->getValue());
    strcpy(mqtt_port, custom_mqtt_port->getValue());
    strcpy(mqtt_api_token, custom_api_token->getValue());

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

void switchToConfigMode(WiFiManager &wifiManager, DynamicJsonDocument &pJson) {
    wifiManager.startConfigPortal();
    EspClass::restart();
    delay(1000);
}

void jsonDiscoverPreset(JsonDocument &json);

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
            if (configFile && configFile.size() > BUFFER_MAX_SIZE) {
                Serial.println("FS or file corrupt");
            } else if (configFile) {
                Serial.println("opened config file");
                size_t size = configFile.size();
                configFile.readBytes(buffer, size);
                auto deserializeError = deserializeJson(json, buffer);
                serializeJson(json, Serial);
                if ( ! deserializeError ) {
                    Serial.println("\nparsed json");
                    strcpy(mqtt_broker, json["mqtt_server"]);
                    strcpy(mqtt_port, json["mqtt_port"]);
                    strcpy(mqtt_api_token, json["api_token"]);
                    Serial.print("The values in the file are: ");
                    Serial.print("\tmqtt_broker : " + String(mqtt_broker));
                    Serial.print("\tmqtt_port : " + String(mqtt_port));
                    Serial.println("\tmqtt_api_token : " + String(mqtt_api_token));
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

    //connecting to a mqtt broker
    WiFiManager wifiManager;
#ifndef DEBUG
    wifiManager.setDebugOutput(false);
#endif
	wifiManager.setConfigPortalTimeout(300);

    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setSaveConfigCallback(saveConfig);

    custom_mqtt_server = new WiFiManagerParameter("server", "mqtt server", mqtt_broker, 40);
    custom_mqtt_port = new WiFiManagerParameter("port", "mqtt port", mqtt_port, 6);
    custom_api_token = new WiFiManagerParameter("apikey", "API token", mqtt_api_token, 32);

    wifiManager.addParameter(custom_mqtt_server);
    wifiManager.addParameter(custom_mqtt_port);
    wifiManager.addParameter(custom_api_token);

    wifi_softap_dhcps_start();
    if(!wifiManager.autoConnect("thermoino")) {
        Serial.println("failed to connect and hit timeout");
        //reset and try again, or maybe put it to deep sleep
        EspClass::restart();
        delay(1000);
    }
    wifi_softap_dhcps_stop();

    if (!MDNS.begin("thermoino")) {
        Serial.println("Error setting up MDNS responder!");
    }

    IPAddress remote_addr;
    if (!WiFi.hostByName(mqtt_broker, remote_addr)) {
        Serial.println("Error resolving mqtt_broker");
    };
    client.setServer(mqtt_broker, strtol(mqtt_port, nullptr, 10));
    client.setCallback(mqttDataCallback);
    uint16_t failures = 0;
    while (!client.connected()) {
		if (++failures > 10) {
			switchToConfigMode(wifiManager, json);
			failures = 0;
		}
        const String client_id("esp8266-client-" + String(EspClass::getChipId()));
        if (client.connect(client_id.c_str(), "thermoino", mqtt_api_token)) {
#ifdef DEBUG
            Serial.printf("The client %s (%s) connected to the mqtt broker\r\n", client_id.c_str(), mqtt_api_token);
#endif
        } else {
#ifdef DEBUG
            Serial.printf("The client %s (%s) ", client_id.c_str(), mqtt_api_token);
#endif
			// state > 0 means more attempts won't help, reset
            Serial.printf("failed to connect to mqtt, state: %d\r\n", client.state());
            if (client.state() > 0) {
				switchToConfigMode(wifiManager, json);
			}
            delay(2000);
        }
    }

    // no save allowed pass here
    delete custom_api_token, delete custom_mqtt_port, delete custom_mqtt_server;
    custom_api_token = custom_mqtt_port = custom_mqtt_server = nullptr;

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Room Temp";
    json["device_class"] = "temperature";
    json["unit_of_measurement"] = "°C";
    json["value_template"] = "{{ value_json.roomTemp }}";
    json["unique_id"] = topicBase.substring(1) + "-roomTemp";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-roomTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Boiler Temp";
    json["device_class"] = "temperature";
    json["unit_of_measurement"] = "°C";
    json["value_template"] = "{{ value_json.boilerTemp }}";
    json["unique_id"] = topicBase.substring(1) + "-boilerTemp";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-boilerTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Heat demand PWM";
    json["value_template"] = "{{ value_json.heatNeededPWM }}";
    json["unique_id"] = topicBase.substring(1) + "-heatNeededPWM";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-heatNeededPWM/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Angle";
    json["device_class"] = "power_factor";
    json["unit_of_measurement"] = "%";
    json["state_class"] = "measurement";
    json["value_template"] = "{{ value_json.angle }}";
    json["unique_id"] = topicBase.substring(1) + "-angle";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-angle/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Circuit Relay";
    json["value_template"] = "{{ value_json.circuitRelay }}";
    json["unique_id"] = topicBase.substring(1) + "-circuitRelay";
    client.beginPublish(("homeassistant/binary_sensor" + topicBase + "-circuitRelay/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Heat demand switch";
    json["value_template"] = "{{ value_json.heatNeeded }}";
    json["unique_id"] = topicBase.substring(1) + "-heatNeeded";
    json["command_topic"] = "~" + heatNeededSetTopic;
    client.beginPublish(("homeassistant/switch" + topicBase + "-heatNeeded/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Boiler ref. temp.";
    json["value_template"] = "{{ value_json.boilerRefTemp }}";
    json["unit_of_measurement"] = "°C";
    json["unique_id"] = topicBase.substring(1) + "-boilerRefTemp";
    json["command_topic"] = "~" + boilerRefTempSetTopic;
    json["state_class"] = "measurement";
    json["min"] = 45.0f;
    json["max"] = 90.0f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerRefTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Room ref. temp.";
    json["value_template"] = "{{ value_json.roomRefTemp }}";
    json["unit_of_measurement"] = "°C";
    json["unique_id"] = topicBase.substring(1) + "-roomRefTemp";
    json["command_topic"] = "~" + roomRefTempSetTopic;
    json["state_class"] = "measurement";
    json["min"] = 10.0f;
    json["max"] = 30.0f;
    client.beginPublish(("homeassistant/number" + topicBase + "-roomRefTemp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
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

    jsonDiscoverPreset(json);
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

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Boiler PID Kp";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKp";
    json["command_topic"] = "~" + boilerPIDKpTopic;
    json["value_template"] = "{{ value_json.boilerPIDKp }}";
    json["stat_t"] = "~/state";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.05f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Boiler PID Ki";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKi";
    json["command_topic"] = "~" + boilerPIDKiTopic;
    json["value_template"] = "{{ value_json.boilerPIDKi }}";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.001f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKi/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Boiler PID Kd";
    json["unique_id"] = topicBase.substring(1) + "-boilerPIDKd";
    json["command_topic"] = "~" + boilerPIDKdTopic;
    json["value_template"] = "{{ value_json.boilerPIDKd }}";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.001f;
    client.beginPublish(("homeassistant/number" + topicBase + "-boilerPIDKd/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Relay PID Kp";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKp";
    json["command_topic"] = "~" + relayPIDKpTopic;
    json["value_template"] = "{{ value_json.relayPIDKp }}";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.05f;
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKp/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Relay PID Ki";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKi";
    json["command_topic"] = "~" + relayPIDKiTopic;
    json["value_template"] = "{{ value_json.relayPIDKi }}";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.001f;
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKi/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "Thermoino Relay PID Kd";
    json["unique_id"] = topicBase.substring(1) + "-relayPIDKd";
    json["command_topic"] = "~" + relayPIDKdTopic;
    json["value_template"] = "{{ value_json.relayPIDKd }}";
    json["min"] = 0.0f;
    json["max"] = 1000.0f;
    json["step"] = 0.001f;
    client.beginPublish(("homeassistant/number" + topicBase + "-relayPIDKd/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.clear();

    Serial.println("State topic: " + generalTopicBase + "/state");
    // publish and subscribe
    client.subscribe((generalTopicBase + heatNeededSetTopic).c_str());
    client.subscribe((generalTopicBase + boilerRefTempSetTopic).c_str());
    client.subscribe((generalTopicBase + roomRefTempSetTopic).c_str());
    client.subscribe((generalTopicBase + buttonToAutomaticSetTopic).c_str());
    client.subscribe((generalTopicBase + ventOpenSetTopic).c_str());

    client.subscribe((generalTopicBase + boilerPIDKpTopic).c_str());
    client.subscribe((generalTopicBase + boilerPIDKiTopic).c_str());
    client.subscribe((generalTopicBase + boilerPIDKdTopic).c_str());

    client.subscribe((generalTopicBase + relayPIDKpTopic).c_str());
    client.subscribe((generalTopicBase + relayPIDKiTopic).c_str());
    client.subscribe((generalTopicBase + relayPIDKdTopic).c_str());

    sendCmdRefreshData();
}

void jsonDiscoverPreset(JsonDocument &json) {
    json.clear();
    const JsonObject &device = json.createNestedObject("device");
    device["identifiers"] = String(EspClass::getChipId());
    device["name"] = "Thermoino";
    json["~"] = generalTopicBase;
    json["stat_t"] = "~/state";
}

#define literal_len(x) (sizeof(x) - 1)
#define PARSE(x) if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));
#define OR_PARSE(x) } else if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));

uint64_t lastChange = 0;
bool hasChange = false;

void loop() {
    MDNS.update();
    recvWithEndMarker();
    if (serialLineBufferDataReady & 0x2) {
        serialLineBufferDataReady = 0x0;
        Serial.println("serial overflow");
    } else if (serialLineBufferDataReady & 0x1) {
        serialLineBufferDataReady = 0x0;
        const String &sBuffer = String(serialLineBuffer);
        // Serial.print("got -> \"");
        // Serial.print(sBuffer);
        // Serial.println("\"");
        if (sBuffer.startsWith("DRQ:")) {
            const String &commandBuffer = sBuffer.substring(4);
            PARSE("RT")
                roomTemp = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("BT")
                boilerTemp = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("BRT")
                boilerRefTemp = strtol(valueBuffer.c_str(), nullptr, 10);
            OR_PARSE("RRT")
                roomRefTemp = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("O")
                angle = strtol(valueBuffer.c_str(), nullptr, 10);
            OR_PARSE("R")
                circuitRelay = strtol(valueBuffer.c_str(), nullptr, 10) != 0;
            OR_PARSE("HN")
                heatNeeded = valueBuffer.equals("true");
            OR_PARSE("HPWM")
                heatNeededPWM = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Kp")
                boilerPIDKp = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Ki")
                boilerPIDKi = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_BL_Kd")
                boilerPIDKd = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Kp")
                relayPIDKp = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Ki")
                relayPIDKi = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("PID_CR_Kd")
                relayPIDKd = strtof(valueBuffer.c_str(), nullptr);
            } else {
//                Serial.println("Unknown command: " + sBuffer);
            }
            hasChange = true;
        }
    }

    if ((millis() - lastChange) > 1000) {
        lastChange = millis();
        if (heatNeededPWM < 0.0f) {
            sendCmdRefreshData();
        }
        if (hasChange) {
            sendState();
            hasChange = false;
        }
    }
    if (!client.connected()) {
        EspClass::restart();
    }
    client.loop();
}

void sendState() {
    json.clear();
    json["roomTemp"] = serialized(String(roomTemp, 2));
    json["boilerTemp"] = serialized(String(boilerTemp, 2));
    json["roomRefTemp"] = serialized(String(roomRefTemp, 2));
    json["boilerRefTemp"] = boilerRefTemp;

    json["angle"] = angle;
    json["circuitRelay"] = circuitRelay ? "ON" : "OFF";
    json["heatNeeded"] = heatNeeded ? "ON" : "OFF";
    json["heatNeededPWM"] = heatNeededPWM;

    json["boilerPIDKp"] = serialized(String(boilerPIDKp, 4));
    json["boilerPIDKi"] = serialized(String(boilerPIDKi, 4));
    json["boilerPIDKd"] = serialized(String(boilerPIDKd, 4));
    json["relayPIDKp"] = serialized(String(relayPIDKp, 4));
    json["relayPIDKi"] = serialized(String(relayPIDKi, 4));
    json["relayPIDKd"] = serialized(String(relayPIDKd, 4));

    const String &topicBaseState = "homeassistant/climate" + topicBase + "/state";
//    Serial.println(topicBaseState);
    client.beginPublish(topicBaseState.c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();
//    serializeJson(doc, Serial);
//    Serial.println();
}


void mqttDataCallback(char* topic, const uint8_t* payload, unsigned int length) {
    String topicStr = String(topic);

    if (!topicStr.startsWith(generalTopicBase)) return;

//    Serial.print("Message arrived [");
//    Serial.print(topic);
//    Serial.print("] ");
    char payloadCStr[length + 1];
    for (unsigned int i = 0; i < length; i++) {
        payloadCStr[i] = (char)payload[i];
    }
    payloadCStr[length] = '\0';
//    Serial.print(payloadCStr);
//    Serial.println();

    if (topicStr.equals(generalTopicBase + heatNeededSetTopic)) {
        const bool lastHeatNeeded = strcasecmp(payloadCStr, "on") == 0;
        if (lastHeatNeeded != heatNeeded) {
            sendCmdHeatOverride(true, lastHeatNeeded);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerRefTempSetTopic)) {
        uint16_t boilerRefTempLocal = strtol(payloadCStr, nullptr, 10);
        if (boilerRefTempLocal != boilerRefTemp) {
            sendBoilerRefTemp(boilerRefTempLocal);
        }
    }

    if (topicStr.equals(generalTopicBase + roomRefTempSetTopic)) {
        float roomRefTempVal = strtof(payloadCStr, nullptr);
        if (roomRefTempVal != roomRefTemp) {
            sendRoomReferenceTemp(roomRefTempVal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKpTopic)) {
        double boilerPIDKpVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKpVal != boilerPIDKp) {
            sendCmdBoilerPIDKp(boilerPIDKpVal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKiTopic)) {
        double boilerPIDKiVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKiVal != boilerPIDKi) {
            sendCmdBoilerPIDKi(boilerPIDKiVal);
        }
    }

    if (topicStr.equals(generalTopicBase + boilerPIDKdTopic)) {
        double boilerPIDKdVal = strtod(payloadCStr, nullptr);
        if (boilerPIDKdVal != boilerPIDKd) {
            sendCmdBoilerPIDKd(boilerPIDKdVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKpTopic)) {
        double relayPIDKpVal = strtod(payloadCStr, nullptr);
        if (relayPIDKpVal != relayPIDKp) {
            sendCmdRelayPIDKp(relayPIDKpVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKiTopic)) {
        double relayPIDKiVal = strtod(payloadCStr, nullptr);
        if (relayPIDKiVal != relayPIDKi) {
            sendCmdRelayPIDKi(relayPIDKiVal);
        }
    }

    if (topicStr.equals(generalTopicBase + relayPIDKdTopic)) {
        double relayPIDKdVal = strtod(payloadCStr, nullptr);
        if (relayPIDKdVal != relayPIDKd) {
            sendCmdRelayPIDKd(relayPIDKdVal);
        }
    }

    if (topicStr.equals(generalTopicBase + ventOpenSetTopic)) {
        const uint16_t ventOpen = strtol(payloadCStr, nullptr, 10);
        sendCmdVentOpen(ventOpen);
    }

    if (topicStr.equals(generalTopicBase + buttonToAutomaticSetTopic)) {
        const bool pressed = strcasecmp(payloadCStr, "press") == 0;
        if (pressed) {
            sendCmdModeAuto();
        }
    }
}

void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

void sendCmdModeAuto() {
    Serial.println("DRQ:M:A");
}

void sendCmdRefreshData() {
    Serial.println("DRQ:INT:SYNC");
}

void sendRoomReferenceTemp(const float roomReferenceTemp) {
    Serial.print("DRQ:RRT:");
    Serial.println(roomReferenceTemp);
}

void sendBoilerRefTemp(const uint16_t boilerRefTempLocal) {
    Serial.print("DRQ:BRT:");
    Serial.println(boilerRefTempLocal);
}

void sendCmdVentOpen(const uint16_t ventOpen) {
    Serial.print("DRQ:O:");
    Serial.println(ventOpen);
}

void sendCmdHeatOverride(bool heatOverride, bool pHeatNeeded) {
    Serial.print("DRQ:HNO:");
    Serial.println(heatOverride ? (pHeatNeeded ? "2" : "1") : "0");
}

void sendCmdBoilerPIDKp(double param) {
    Serial.print("DRQ:PID_BL_Kp:");
    Serial.println(param, 4);
}

void sendCmdBoilerPIDKi(double param) {
    Serial.print("DRQ:PID_BL_Ki:");
    Serial.println(param, 4);
}

void sendCmdBoilerPIDKd(double param) {
    Serial.print("DRQ:PID_BL_Kd:");
    Serial.println(param, 4);
}

void sendCmdRelayPIDKp(double param) {
    Serial.print("DRQ:PID_CR_Kp:");
    Serial.println(param, 4);
}

void sendCmdRelayPIDKi(double param) {
    Serial.print("DRQ:PID_CR_Ki:");
    Serial.println(param, 4);
}

void sendCmdRelayPIDKd(double param) {
    Serial.print("DRQ:PID_CR_Kd:");
    Serial.println(param, 4);
}
