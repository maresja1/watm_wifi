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
#include "watm_wifi.h"

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

double volume = 0;
float volumeFlow = 0;
uint64_t volumePulses = 0;
uint32_t volumeFlowPulses = 0;

const String &topicBase = String("/watm_") + EspClass::getChipId();
const String &generalTopicBase = "homeassistant/climate" + topicBase;
const String &topicBaseState = generalTopicBase + "/state";
const String &buttonResetTopic = String("/reset/set");
const String &qDivTopic = String("/qDiv/set");
const String &qOffsetTopic = String("/qOffset/set");

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
    if(!wifiManager.autoConnect("watm")) {
        Serial.println("failed to connect and hit timeout");
        //reset and try again, or maybe put it to deep sleep
        EspClass::restart();
        delay(1000);
    }
    wifi_softap_dhcps_stop();

    if (!MDNS.begin("watm")) {
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
        if (client.connect(client_id.c_str(), "watm", mqtt_api_token)) {
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
    json["name"] = "watm Volume";
    json["device_class"] = "volume";
    json["unit_of_measurement"] = "L";
    json["value_template"] = "{{ value_json.volume }}";
    json["unique_id"] = topicBase.substring(1) + "-volume";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-volume/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm Volume Flow";
    json["device_class"] = "volume_flow_rate";
    json["unit_of_measurement"] = "L/min";
    json["value_template"] = "{{ value_json.volumeFlow }}";
    json["unique_id"] = topicBase.substring(1) + "-volumeFlow";
    client.beginPublish(("homeassistant/sensor" + topicBase + "-volumeFlow/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm Volume Flow Pulses";
    json["value_template"] = "{{ value_json.volumeFlowPulses }}";
    json["state_class"] = "measurement";
    json["unique_id"] = topicBase.substring(1) + "-volumeFlowPulses";
    client.beginPublish(("homeassistant/number" + topicBase + "-volumeFlowPulses/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm Volume Pulses";
    json["value_template"] = "{{ value_json.volumePulses }}";
    json["state_class"] = "measurement";
    json["unique_id"] = topicBase.substring(1) + "-volumePulses";
    client.beginPublish(("homeassistant/number" + topicBase + "-volumePulses/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm qDiv";
    json["value_template"] = "{{ value_json.qDiv }}";
    json["unique_id"] = topicBase.substring(1) + "-qDiv";
    json["command_topic"] = "~" + qDivTopic;
    json["suggested_display_precision"] = 1;
    json["min"] = 0;
    json["max"] = 12;
    json["step"] = 0.1f;
    client.beginPublish(("homeassistant/number" + topicBase + "-qDiv/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm qOffset";
    json["value_template"] = "{{ value_json.qOffset }}";
    json["unique_id"] = topicBase.substring(1) + "-qOffset";
    json["command_topic"] = "~" + qOffsetTopic;
    json["suggested_display_precision"] = 1;
    json["min"] = -10;
    json["max"] = 10;
    json["step"] = 0.1f;
    client.beginPublish(("homeassistant/number" + topicBase + "-qOffset/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    jsonDiscoverPreset(json);
    json["name"] = "watm buttonReset";
    json["unique_id"] = topicBase.substring(1) + "-buttonReset";
    json["command_topic"] = "~" + buttonResetTopic;
    client.beginPublish(("homeassistant/button" + topicBase + "-buttonReset/config").c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();

    json.clear();

    client.subscribe((generalTopicBase + buttonResetTopic).c_str());
    client.subscribe((generalTopicBase + qDivTopic).c_str());
    client.subscribe((generalTopicBase + qOffsetTopic).c_str());
    Serial.println("State topic: " + topicBaseState);
}

void jsonDiscoverPreset(JsonDocument &json) {
    json.clear();
    const JsonObject &device = json.createNestedObject("device");
    device["identifiers"] = String(EspClass::getChipId());
    device["name"] = "watm";
    json["~"] = generalTopicBase;
    json["stat_t"] = "~/state";
}

#define literal_len(x) (sizeof(x) - 1)
#define PARSE(x) if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));
#define OR_PARSE(x) } else if (commandBuffer.startsWith(F(x ":"))) { const String &valueBuffer = commandBuffer.substring(literal_len(x ":"));

uint64_t lastChange = 0;
uint64_t lastRefresh = 0;

float qOffset = 0.0f;
float qDiv = 6.6f;

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
            PARSE("V")
                volume = strtod(valueBuffer.c_str(), nullptr);
            OR_PARSE("VP")
                volumePulses = strtoull(valueBuffer.c_str(), nullptr, 10);
            OR_PARSE("VF")
                volumeFlow = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("VFP")
                volumeFlowPulses = strtoul(valueBuffer.c_str(), nullptr, 10);
            OR_PARSE("QO")
                qOffset = strtof(valueBuffer.c_str(), nullptr);
            OR_PARSE("QD")
                qDiv = strtof(valueBuffer.c_str(), nullptr);
            } else {
//                Serial.println("Unknown command: " + sBuffer);
            }
            hasChange = true;
        }
    }

    if (millis() - lastChange > 1000) {
        lastChange = millis();
        if (hasChange) {
            sendState();
            hasChange = false;
        }
    }

     if (millis() - lastRefresh > 60000) {
         lastRefresh = millis();
         sendCmdRefreshData();
     }

    if (!client.connected()) {
        EspClass::restart();
    }
    client.loop();
}

void sendState() {
    json.clear();
    json["volume"] = serialized(String(volume, 2));
    json["volumePulses"] = serialized(String(volumePulses));
    json["volumeFlow"] = serialized(String(volumeFlow, 2));
    json["volumeFlowPulses"] = serialized(String(volumeFlowPulses));
    json["qDiv"] = serialized(String(qDiv, 2));
    json["qOffset"] = serialized(String(qOffset, 2));
//    Serial.println(topicBaseState);
    client.beginPublish(topicBaseState.c_str(), measureJson(json), true);
    serializeJson(json, client);
    client.endPublish();
//    serializeJson(doc, Serial);
//    Serial.println();
}

void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

void sendCmdRefreshData() {
    Serial.println("DRQ:INT:SYNC");
}

void sendCmdReset() {
    Serial.println("DRQ:INT:RES");
}

void sendQDiv(const float qDiv) {
    Serial.print("DRQ:QD:");
    Serial.println(qDiv);
}

void sendQOffset(const float qOffset) {
    Serial.print("DRQ:QO:");
    Serial.println(qOffset);
}

void mqttDataCallback(char* topic, const uint8_t* payload, unsigned int length) {
    String topicStr = String(topic);

    if (!topicStr.startsWith(generalTopicBase)) return;

    // Serial.print("Message arrived [");
    // Serial.print(topic);
    // Serial.print("] ");
    char payloadCStr[length + 1];
    for (unsigned int i = 0; i < length; i++) {
        payloadCStr[i] = (char)payload[i];
    }
    payloadCStr[length] = '\0';
//    Serial.print(payloadCStr);
//    Serial.println();

    if (topicStr.equals(generalTopicBase + qDivTopic)) {
        float qDivNew = strtof(payloadCStr, nullptr);
        if (qDiv != qDivNew) {
            sendQDiv(qDivNew);
            qDiv = qDivNew;
        }
    }

    if (topicStr.equals(generalTopicBase + qOffsetTopic)) {
        float qOffsetNew = strtof(payloadCStr, nullptr);
        if (qOffset != qOffsetNew) {
            sendQDiv(qOffsetNew);
            qOffset = qOffsetNew;
        }
    }

    if (topicStr.equals(generalTopicBase + buttonResetTopic)) {
        const bool pressed = strcasecmp(payloadCStr, "press") == 0;
        if (pressed) {
            sendCmdReset();
        }
    }
}
