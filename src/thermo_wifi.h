//
// Created by jan on 4/18/22.
//

#ifndef THERMO_WIFI_H
#define THERMO_WIFI_H

void configModeCallback (WiFiManager *myWiFiManager);
void mqttDataCallback(char *topic, uint8_t* payload, unsigned int length);

void sendState();

void sendCmdVentOpen(const int32_t ventOpen);

void sendCmdRefreshData();
void sendCmdModeAuto();

void sendCmdBoilerPIDKp(double param);
void sendCmdBoilerPIDKi(double boilerPIDKpVal);
void sendCmdBoilerPIDKd(double boilerPIDKpVal);

void sendCmdRelayPIDKp(double relayPIDKpVal);
void sendCmdRelayPIDKi(double relayPIDKpVal);
void sendCmdRelayPIDKd(double relayPIDKpVal);

void sendCmdHeatOverride(bool heatOverride, bool heatNeeded);

#endif //THERMO_WIFI_H
