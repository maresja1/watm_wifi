//
// Created by jan on 4/18/22.
//

#ifndef THERMO_WIFI_H
#define THERMO_WIFI_H

void configModeCallback (WiFiManager *myWiFiManager);

void sendState();
void mqttDataCallback(char *topic, const uint8_t* payload, unsigned int length);

void sendCmdRefreshData();

#endif //THERMO_WIFI_H
