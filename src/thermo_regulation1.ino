#include <Arduino.h>
#include <EEPROM.h>

#include <Servo.h>

// include the library code:
#include <LiquidCrystal_I2C.h>

#include "Thermoino.h"

#define DEBUG_LEVEL 2
#define SERVO_PIN 6
#define CIRCUIT_RELAY_PIN 7
#define BTN_1_PIN 5
#define BTN_2_PIN 4
#define BTN_3_PIN 3
#define BTN_4_PIN 2
#define BOILER_THERM_PIN A0
#define ROOM_THERM_PIN A1

// initialize the library with the numbers of the interface pins
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

#define MAX_DELTA_SETTINGS 10

int8_t maxDeltaSettings[MAX_DELTA_SETTINGS] = {-20, -12, -5, 5, 10, 0, 0, 0, 0, 0};
uint8_t maxDeltaHigh[MAX_DELTA_SETTINGS] = {90, 50, 25, 12, 0, 0, 0, 0, 0, 0};

Configuration config = {
    .refTempBoiler = 70,
    .refTempBoilerIdle = 50,
    .refTempRoom = 22.0f,
    .circuitRelayForced = 0,
    .servoMin = 0,
    .servoMax = 180,
    .curveItems = 5,
    .debounceLimitC = 2.0f,
    .underheatingLimit = 45,
    .overheatingLimit = 80,
    // least squares of (1,2), (20,6), (44,12) - y = 0.2333x + 1.612
    .deltaTempPoly1 = 0.2333f,
    .deltaTempPoly0 = 1.612f
};

#define MAX_BUFFER_LEN 20
char buffer[20];

uint32_t ticks = 0;
uint8_t angle = 100;
int16_t settingsSelected = -1;
float boilerTemp = 0.0f;
float roomTemp = 0.0f;
bool heatNeeded = false;
bool overheating = false;
bool underheating = false;
bool circuitRelay = false;
bool stateChangedSinceRefresh = true;
bool stateChangedSinceLongRefresh = true;

void eepromInit();

float readTemp(uint8_t pin);

bool processSettings();

void printStatus();

void servoSetPos(int positionPercent);

void servoInit();

void setup()
{
    Serial.begin(9600);

    while (!Serial) { ; // wait for serial port to connect. Needed for native USB port only
    }

    servoInit();
    analogReference(DEFAULT);
    pinMode(SERVO_PIN, OUTPUT);
    pinMode(CIRCUIT_RELAY_PIN, OUTPUT);
    pinMode(BTN_1_PIN, INPUT);
    pinMode(BTN_2_PIN, INPUT);
    pinMode(BTN_3_PIN, INPUT);
    pinMode(BTN_4_PIN, INPUT);

    // set up the LCD's number of columns and rows:
    lcd.begin(16, 2);

    eepromInit();
    Serial.println("Thermoino 1, built:" __DATE__ " " __TIME__ " (" __FILE__ ") - Setup finished.");
}

void loop()
{
    const int lastSettings = settingsSelected;
    const bool buttonsChanged = processSettings();
    const float lastBoilerTemp = boilerTemp;
    const float lastRoomTemp = roomTemp;

    roomTemp = readTemp(ROOM_THERM_PIN);
    boilerTemp = readTemp(BOILER_THERM_PIN);
    boilerTemp += (config.deltaTempPoly1 * (boilerTemp - roomTemp)) + config.deltaTempPoly0;

    angle = 100;

    heatNeeded = (heatNeeded && (roomTemp - config.refTempRoom <= (config.debounceLimitC / 2))) ||
                 (roomTemp - config.refTempRoom <= -(config.debounceLimitC / 2));

    const float boilerDelta = boilerTemp - (heatNeeded ? config.refTempBoiler : config.refTempBoilerIdle);

    overheating = boilerTemp > config.overheatingLimit;
    underheating = boilerTemp < config.underheatingLimit;

    // adjust when angle depends on something else as well
    const bool inputsChanged = (lastSettings != settingsSelected) ||
        (boilerTemp != lastBoilerTemp) ||
        (roomTemp != lastRoomTemp);

    for (int i = config.curveItems - 1; i >= 0; i--) {
        if (boilerDelta >= maxDeltaSettings[i]) {
            int nextAngle = maxDeltaHigh[i];
            int nextI = i + 1;
            if (nextI < config.curveItems) {
                // linear interpolation
                nextAngle = float(maxDeltaHigh[i]) +
                    (
                        (boilerDelta - maxDeltaSettings[i]) *
                        (
                            float(maxDeltaHigh[nextI] - maxDeltaHigh[i]) /
                            float(maxDeltaSettings[nextI] - maxDeltaSettings[i])
                        )
                    );
            }
            angle = nextAngle;
            break;
        }
    }

    const bool stateChanged = buttonsChanged || inputsChanged;

    circuitRelay = !underheating && (heatNeeded || overheating);

    const bool circuitRelayOrOverride = config.circuitRelayForced == 0 ?
        circuitRelay : config.circuitRelayForced == 1;

    // safety mechanism
    if (boilerTemp > 85) {
        angle = 0;
        circuitRelay = true;
    }

    stateChangedSinceRefresh |= stateChanged;
    stateChangedSinceLongRefresh |= stateChanged;

    if (settingsSelected == 3) {
        // override servo driving for adjusting purposes
        angle = 100;
        servoSetPos(angle);
        if (buttonsChanged) {
            printStatus();
        }
    } else if (settingsSelected == 4) {
        // override servo driving for adjusting purposes
        angle = 0;
        servoSetPos(angle);
        if (buttonsChanged) {
            printStatus();
        }
    } else if (settingsSelected >= 0) {
        // in settings menu refresh after every button pressed
        if (buttonsChanged) {
            printStatus();
        }
    } else if (lastSettings != settingsSelected) {
        // when menu page changed, refresh
        printStatus();
    } else if (ticks % 50 == 0 && stateChangedSinceLongRefresh) {
        // long refresh loop
        printStatus();
        servoSetPos(angle);
        digitalWrite(CIRCUIT_RELAY_PIN, circuitRelayOrOverride);
        stateChangedSinceLongRefresh = false;
    }

    if (stateChangedSinceRefresh && ((ticks % 10) == 0)) {
        printStatus();
        stateChangedSinceRefresh = false;
#if DEBUG_LEVEL > 1
        if (inputsChanged) {
            Serial.print("boilerTemp: ");
            Serial.print(boilerTemp);
            Serial.print(", roomTemp: ");
            Serial.print(roomTemp);
            Serial.print(", angle: ");
            Serial.print(angle);
            Serial.print(", overheating: ");
            Serial.print(overheating);
            Serial.print(", underheating: ");
            Serial.print(underheating);
            Serial.println("");
        }
#endif
    }

    ticks += 1;

    delay(100);
}

// (1/T_0) - 25 => 1 / (25 + 237.15) => 0.00381461f
#define THERM_RESIST_SERIAL 10000
#define THERM_REF_TEMP_INV 0.003354016f
#define THERM_REF_RESIST 10000
#define THERM_BETA 3977
#define DEBUG_SER_PRINT(x) do { Serial.print(#x": "); Serial.print(x); Serial.println(""); } while(0);

float readTemp(uint8_t pin)
{
    int V_out = analogRead(pin);
    // R_1 = R_2 * ( V_in / V_out - 1 ); V_in = 1023 (3.3V after conv.), R_2 = #THERM_RESIST_SERIAL
    float R_1 = float(THERM_RESIST_SERIAL * (float(1023 - V_out) / float(V_out)));
#if DEBUG_LEVEL > 2
    DEBUG_SER_PRINT(V_out)
    DEBUG_SER_PRINT(R_1)
#endif
    // 1 / T = 1 / T_0 + 1 / B * ln(R_1 / R_0); (1 / T_0) = #THERM_REF_TEMP_INV, R_0 = #THERM_REF_RESIST, B = #THERM_BETA
    float T = float((1 / (THERM_REF_TEMP_INV + (log(R_1 / THERM_REF_RESIST) / THERM_BETA))) - 273.15f);
    return lround(T * 16) / 16.0f;
}

int8_t readButton(Button_t button)
{
    int btn = digitalRead(button.pin);
    if (btn != *button.state) {
        *button.state = btn;
        if (btn == LOW) {
            return 1; // released
        } else if (btn == HIGH) {
            return -1; // pressed
        }
    }
    return 0;
}


void *menuHandlerBoiler(__attribute__((unused)) void *param, int8_t diff)
{
    config.refTempBoiler += diff;
    return &config.refTempBoiler;
}

void *menuHandlerBoilerIdle(__attribute__((unused)) void *param, int8_t diff)
{
    config.refTempBoilerIdle += diff;
    return &config.refTempBoilerIdle;
}

void *menuHandlerRoom(__attribute__((unused)) void *param, int8_t diff)
{
    config.refTempRoom += float(diff) * 0.2f;
    return &config.refTempRoom;
}

void *menuHandlerDebounceLimitC(__attribute__((unused)) void *param, int8_t diff)
{
    config.debounceLimitC += float(diff) * 0.1f;
    if (config.debounceLimitC <= -10) {
        config.debounceLimitC = 9.9;
    }
    if (config.debounceLimitC >= 10) {
        config.debounceLimitC = -9.9;
    }
    return &config.debounceLimitC;
}

void *menuHandlerCurveItems(__attribute__((unused)) void *param, int8_t diff)
{
    config.curveItems += diff;
    config.curveItems %= MAX_DELTA_SETTINGS;
    if (config.curveItems < 0) {
        config.curveItems = 0;
    }
    return &config.curveItems;
}

void *menuHandlerCurveItemX(void *param, int8_t diff)
{
    uintptr_t index = (uintptr_t) param;
#if DEBUG_LEVEL > 2
    Serial.print("menuHandlerCurveItemX - index: ");
    Serial.print(index);
    Serial.print(", value: ");
    Serial.print(maxDeltaSettings[index]);
    Serial.print(", diff: ");
    Serial.print(diff);
    Serial.println("");
#endif
    maxDeltaSettings[index] += diff;
    return &maxDeltaSettings[index];
}

void *menuHandlerCurveItemY(void *param, int8_t diff)
{
    uintptr_t index = (uintptr_t) param;
#if DEBUG_LEVEL > 2
    Serial.print("menuHandlerCurveItemY - index: ");
    Serial.print(index);
    Serial.print(", value: ");
    Serial.print(maxDeltaHigh[index]);
    Serial.print(", diff: ");
    Serial.print(diff);
    Serial.println("");
#endif
    maxDeltaHigh[index] += diff;
    return &maxDeltaHigh[index];
}

void *menuHandlerCircuitRelayForced(__attribute__((unused)) void *param, int8_t diff)
{
    if (diff != 0) {
        config.circuitRelayForced = (config.circuitRelayForced + 1) % 3;
    }
    return &config.circuitRelayForced;
}

void *menuHandlerServoMin(__attribute__((unused)) void *param, int8_t diff)
{
    config.servoMin += int16_t(diff);
    return &config.servoMin;
}

void *menuHandlerServoMax(__attribute__((unused)) void *param, int8_t diff)
{
    config.servoMax += int16_t(diff);
    return &config.servoMax;
}

void *menuHandlerOverheatingLimit(__attribute__((unused)) void *param, int8_t diff)
{
    config.overheatingLimit += diff;
    return &config.overheatingLimit;
}

void *menuHandlerUnderheatingLimit(__attribute__((unused)) void *param, int8_t diff)
{
    config.underheatingLimit += diff;
    return &config.underheatingLimit;
}

void *menuHandlerDeltaTempPoly0(__attribute__((unused)) void *param, int8_t diff)
{
    config.deltaTempPoly0 += float(diff) * 0.1f;
    return &config.deltaTempPoly0;
}

void *menuHandlerDeltaTempPoly0S(__attribute__((unused)) void *param, int8_t diff)
{
    config.deltaTempPoly0 += float(diff) * 0.002f;
    return &config.deltaTempPoly0;
}

void *menuHandlerDeltaTempPoly1(__attribute__((unused)) void *param, int8_t diff)
{
    config.deltaTempPoly1 += float(diff) * 0.1f;
    return &config.deltaTempPoly1;
}

void *menuHandlerDeltaTempPoly1S(__attribute__((unused)) void *param, int8_t diff)
{
    config.deltaTempPoly1 += float(diff) * 0.002f;
    return &config.deltaTempPoly1;
}


void menuFormatterUInt8Value(__attribute__((unused)) void *param, char *pBuffer, int16_t maxLen, void *value)
{
    lcd.cursor();
    snprintf(pBuffer, maxLen, "value: %8d", *(uint8_t *) value);
}

void menuFormatterInt16Value(__attribute__((unused)) void *param, char *pBuffer, int16_t maxLen, void *value)
{
    lcd.cursor();
    snprintf(pBuffer, maxLen, "value: %8d", *(int16_t *) value);
}

void menuFormatterInt8Value(__attribute__((unused)) void *param, char *pBuffer, int16_t maxLen, void *value)
{
    lcd.cursor();
    snprintf(pBuffer, maxLen, "value: %8d", *(int8_t *) value);
}

void menuFormatterFloatValue(__attribute__((unused)) void *param, char *pBuffer, int16_t maxLen, void *value)
{
    lcd.cursor();
    snprintf(pBuffer, maxLen, "value: %7.3f", (double)*(float*)value);
}

void menuFormatterCircuitOverride(__attribute__((unused)) void *param, char *pBuffer, int16_t maxLen, void *value)
{
    switch (*(int8_t *) value) {
        case 0:
            snprintf(pBuffer, maxLen, ("no override"));
            break;
        case 1:
            snprintf(pBuffer, maxLen, ("always enabled"));
            break;
        case 2:
            snprintf(pBuffer, maxLen, ("always disabled"));
            break;
    }
}

#define MENU_STATIC_ITEMS 14
const ConfigMenuItem_t menu[] = {
    {
        .name = "Boiler \xDF",
        .param = nullptr,
        .handler = &menuHandlerBoiler,
        .formatter = &menuFormatterUInt8Value
    },
    {
        .name = "Room \xDF",
        .param = nullptr,
        .handler = &menuHandlerRoom,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "Circuit Relay",
        .param = nullptr,
        .handler = &menuHandlerCircuitRelayForced,
        .formatter = &menuFormatterCircuitOverride
    },
    {
        .name = "[E] Servo Min",
        .param = nullptr,
        .handler = &menuHandlerServoMin,
        .formatter = &menuFormatterInt16Value
    },
    {
        .name = "[E] Servo Max",
        .param = nullptr,
        .handler = &menuHandlerServoMax,
        .formatter = &menuFormatterInt16Value
    },
    {
        .name = "[E] Boiler Idle",
        .param = nullptr,
        .handler = &menuHandlerBoilerIdle,
        .formatter = &menuFormatterUInt8Value
    },
    {
        .name = "[E] T. Debounce",
        .param = nullptr,
        .handler = &menuHandlerDebounceLimitC,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "[E] Overheating\xDF",
        .param = nullptr,
        .handler = &menuHandlerOverheatingLimit,
        .formatter = &menuFormatterUInt8Value
    },
    {
        .name = "[E] Underheating\xDF",
        .param = nullptr,
        .handler = &menuHandlerUnderheatingLimit,
        .formatter = &menuFormatterUInt8Value
    },
    {
        .name = "[E] deltaT p1",
        .param = nullptr,
        .handler = &menuHandlerDeltaTempPoly1,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "[E] deltaT p1 S",
        .param = nullptr,
        .handler = &menuHandlerDeltaTempPoly1S,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "[E] deltaT p0",
        .param = nullptr,
        .handler = &menuHandlerDeltaTempPoly0,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "[E] deltaT p0 S",
        .param = nullptr,
        .handler = &menuHandlerDeltaTempPoly0S,
        .formatter = &menuFormatterFloatValue
    },
    {
        .name = "[E] Curve Items",
        .param = nullptr,
        .handler = &menuHandlerCurveItems,
        .formatter = &menuFormatterUInt8Value
    }
};

char bufferMenuName[20];
struct ConfigMenuItem bufferMenuItem;

const struct ConfigMenuItem *getMenu(int16_t itemIndex)
{
    if (itemIndex < MENU_STATIC_ITEMS) {
        return &menu[itemIndex];
    } else {
        uint16_t index = itemIndex - MENU_STATIC_ITEMS;
        bool isY = (index % 2) == 1;
        uintptr_t i = index / 2;
        snprintf(bufferMenuName, 20, ("[E] Curve[%d].%s"), i, isY ? "%" : "d\xDF");
        bufferMenuItem.name = bufferMenuName;
        bufferMenuItem.handler = isY ? &menuHandlerCurveItemY : &menuHandlerCurveItemX;
        bufferMenuItem.formatter = isY ? &menuFormatterUInt8Value : &menuFormatterInt8Value;
        bufferMenuItem.param = (void *) i;
        return &bufferMenuItem;
    }
}

uint8_t btnStates[4] = {LOW, LOW, LOW, LOW};
const Button_t btn1 = {
    .pin = BTN_1_PIN,
    .state = &btnStates[0]
};
const Button_t btn2 = {
    .pin = BTN_2_PIN,
    .state = &btnStates[1]
};
const Button_t btn3 = {
    .pin = BTN_3_PIN,
    .state = &btnStates[2]
};
const Button_t btn4 = {
    .pin = BTN_4_PIN,
    .state = &btnStates[3]
};

Servo servo;

void servoInit()
{
    servo.attach(SERVO_PIN);
}

void servoSetPos(int positionPercent)
{
    // int time = 1500 + ((positionPercent - 50)*step);
    float multi = float(config.servoMax - config.servoMin) / 100.0f;
    servo.write(int16_t((100.0f - positionPercent + config.servoMin) * float(multi)));
}

void printSettings()
{
    lcd.noCursor();
    const ConfigMenuItem_t *currentItem = getMenu(settingsSelected);

    lcd.setCursor(0, 0);
    snprintf(buffer, MAX_BUFFER_LEN, "%s ", currentItem->name);
    lcd.print(buffer);

    lcd.setCursor(0, 1);
    currentItem->formatter(currentItem->param, buffer, MAX_BUFFER_LEN, currentItem->handler(currentItem->param, 0));
    lcd.print(buffer);
    lcd.setCursor(strlen(buffer), 1);
}

char yesOrNo(int input);

void printStatusOverviewTop()
{
    lcd.noCursor();
    lcd.setCursor(0, 0);
    snprintf(buffer, MAX_BUFFER_LEN, "O %4d%%  ", angle);
    lcd.print(buffer);
    lcd.setCursor(8, 0);
    snprintf(
        buffer,
        MAX_BUFFER_LEN,
        ("S %c%c%c"),
        config.circuitRelayForced != 0 ? 'O' : (
            yesOrNo(circuitRelay)
        ),
        config.circuitRelayForced == 0 ? ' ' : (
            yesOrNo(config.circuitRelayForced == 1)
        ),
        yesOrNo(heatNeeded)
    );
    lcd.print(buffer);
}

void printStatusOverviewBottom()
{
    lcd.noCursor();
    lcd.setCursor(0, 1);
    // 2 + 4 + 2 = 8 chars
    snprintf(buffer, MAX_BUFFER_LEN, "B %4.1f\xDF ", (double)boilerTemp);
    lcd.print(buffer);
    lcd.setCursor(8, 1);
    // 2 + 4 + 1 = 7 chars
    snprintf(buffer, MAX_BUFFER_LEN, "R %4.1f\xDF", (double)roomTemp);
    lcd.print(buffer);
}

void printStatusOverview()
{
    printStatusOverviewTop();
    printStatusOverviewBottom();
}

void printStatus()
{
    if (settingsSelected >= 0) {
        printSettings();
    } else {
        printStatusOverview();
    }
}

#define EXPERT_MENU_ITEMS
#define MAX_MENU_ITEMS (MENU_STATIC_ITEMS + (config.curveItems * 2) + 1)

void eepromUpdate();

bool processSettings()
{
    bool stateChanged = false;
    if (readButton(btn1) == 1) {
        stateChanged = true;
        settingsSelected = settingsSelected - 1;
        if (settingsSelected == -2) {
            settingsSelected = MAX_MENU_ITEMS - 2;
        }
        lcd.clear();
    }
    if (readButton(btn2) == 1) {
        stateChanged = true;
        settingsSelected = settingsSelected + 1;
        if (settingsSelected == MAX_MENU_ITEMS - 1) {
            settingsSelected = -1;
        }
        lcd.clear();
    }
    if (settingsSelected >= 0) {
        const ConfigMenuItem_t *currentItem = getMenu(settingsSelected);
        if (readButton(btn3) == 1) {
            stateChanged = true;
            currentItem->handler(currentItem->param, -1);
        }
        if (readButton(btn4) == 1) {
            stateChanged = true;
            currentItem->handler(currentItem->param, 1);
        }
        if (stateChanged) {
            eepromUpdate();
        }
    }
    return stateChanged;
}


#define EEPROM_MAGIC 0xDEADBEEF

void eepromInit()
{
    uint32_t checkCode;
    EEPROM.get(0, checkCode);
    if (checkCode == EEPROM_MAGIC && false) {
        int offset = sizeof(checkCode);
        EEPROM.get(offset, config);
        offset += sizeof(config);
        EEPROM.get(offset, maxDeltaSettings);
        offset += sizeof(maxDeltaSettings);
        EEPROM.get(offset, maxDeltaHigh);
    } else {
        eepromUpdate();
    }
}

void eepromUpdate()
{
    uint32_t checkCode = EEPROM_MAGIC;
    int offset = 0;
    EEPROM.put(offset, checkCode);
    offset += sizeof(checkCode);
    EEPROM.put(offset, config);
    offset += sizeof(config);
    EEPROM.put(offset, maxDeltaSettings);
    offset += sizeof(maxDeltaSettings);
    EEPROM.put(offset, maxDeltaHigh);
}

char yesOrNo(int input)
{
    return input != 0 ? '\xff' : '\xdb';
}