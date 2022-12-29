//
// Created by jan on 4/18/22.
//

#ifndef THERMO_WIFI_H
#define THERMO_WIFI_H

void configModeCallback (WiFiManager *myWiFiManager);
void mqttDataCallback(char *topic, const uint8_t* payload, unsigned int length);

void sendState();
void parseThreeFloats(const String& sBuffer, float *dFirst, float *dSecond, float *dThree);

void sendCmdVentOpen(uint16_t ventOpen);
void sendCmdRefreshData();
void sendRoomReferenceTemp(float boilerRefTempLocal);
void sendBoilerRefTemp(uint16_t boilerRefTempLocal);
void sendCmdModeAuto();

void sendCmdBoilerPIDKp(double param);
void sendCmdBoilerPIDKi(double boilerPIDKpVal);
void sendCmdBoilerPIDKd(double boilerPIDKpVal);

void sendCmdRelayPIDKp(double relayPIDKpVal);
void sendCmdRelayPIDKi(double relayPIDKpVal);
void sendCmdRelayPIDKd(double relayPIDKpVal);

void sendCmdHeatOverride(bool heatOverride, bool heatNeeded);

#endif //THERMO_WIFI_H
