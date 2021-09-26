#include <EEPROM.h>

#include <Servo.h>

// include the library code:
#include <LiquidCrystal.h>

Servo servo;

#define SERVO_PIN 6
#define CIRCUIT_RELAY_PIN 7
#define BTN_1_PIN 5
#define BTN_2_PIN 4
#define BTN_3_PIN 3
#define BTN_4_PIN 2

const int minV = 58;
const int minTemp = -40;
// (maxTemp=125 - minTemp) / (maxV - minV=995)
const float resTemp = 5.824242424f;

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

void setup() {
    Serial.begin(9600);

    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    servo.attach(SERVO_PIN);
    analogReference(EXTERNAL);
    pinMode(SERVO_PIN, OUTPUT);
    pinMode(CIRCUIT_RELAY_PIN, OUTPUT);
    pinMode(BTN_1_PIN, INPUT);
    pinMode(BTN_2_PIN, INPUT);
    pinMode(BTN_3_PIN, INPUT);
    pinMode(BTN_4_PIN, INPUT);
    // set up the LCD's number of columns and rows:
    lcd.begin(16, 2);

    eepromInit();
    Serial.println("Setup finished.");
}

float readTemp(int port) {
    int mV = analogRead(port);
    return ((mV - minV) / resTemp) + minTemp;

}

int8_t readButton(uint8_t button[2]) {
    int btn = digitalRead(button[0]);
    if (btn != button[1]) {
        button[1] = btn;
        if (btn == LOW) {
            return 1; // released
        } else if (btn == HIGH) {
            return -1; // pressed
        }
    }
    return 0;
}


typedef struct ConfigMenuItem {
    const char *name;
    void* param;
    void* (*handler)(void* param, int8_t diff);
    void (*formatter)(void* param, char *buffer, int16_t maxLen, void *value);
} ConfigMenuItem_t;

#define MAX_DELTA_SETTINGS 10

struct Configuration {
    uint8_t refTempBoiler;
    uint8_t refTempBoilerIdle;
    float refTempRoom;
    uint8_t circuitRelayForced;
    int16_t servoMin;
    int16_t servoMax;
    uint8_t curveItems;
    float debounceLimitC;
    uint8_t underheatingLimit;
    uint8_t overheatingLimit;
};

int8_t maxDeltaSettings[] = {-20, -12, -5, 5, 10, 0, 0, 0, 0, 0};
uint8_t maxDeltaHigh[] = {90, 50, 25, 12, 0, 0, 0, 0, 0, 0};

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
};

uint32_t ticks = 0;
uint8_t angle = 100;
int16_t settingsSelected = -1;
char buffer[40];
char floatStrTemp[6];
float boilerTemp = 0.0f;
float roomTemp = 0.0f;
bool heatNeeded = false;
bool overheating = false;
bool underheating = false;
bool circuitRelay = false;

void *menuHandlerBoiler(void* param, int8_t diff) {
    config.refTempBoiler += diff;
    return &config.refTempBoiler;
}

void *menuHandlerBoilerIdle(void* param, int8_t diff) {
    config.refTempBoilerIdle += diff;
    return &config.refTempBoilerIdle;
}

void *menuHandlerRoom(void* param, int8_t diff) {
    config.refTempRoom += float(diff) * 0.2f;
    return &config.refTempRoom;
}

void *menuHandlerDebounceLimitC(void* param, int8_t diff) {
    config.debounceLimitC += float(diff) * 0.1f;
    if (config.debounceLimitC <= -10) {
        config.debounceLimitC = 9.9;
    }
    if (config.debounceLimitC >= 10) {
        config.debounceLimitC = -9.9;
    }
    return &config.debounceLimitC;
}

void *menuHandlerCurveItems(void* param, int8_t diff) {
    config.curveItems += diff;
    config.curveItems %= MAX_DELTA_SETTINGS;
    if (config.curveItems < 0) {
        config.curveItems = 0;
    }
    return &config.curveItems;
}

void *menuHandlerCurveItemX(void* param, int8_t diff) {
    uintptr_t index = (uintptr_t)param;
    Serial.print("menuHandlerCurveItemX - index: ");
    Serial.print(index);
    Serial.print(", value: ");
    Serial.print(maxDeltaSettings[index]);
    Serial.print(", diff: ");
    Serial.print(diff);
    Serial.println("");
    maxDeltaSettings[index] += diff;
    return &maxDeltaSettings[index];
}

void *menuHandlerCurveItemY(void* param, int8_t diff) {
    uintptr_t index = (uintptr_t)param;
    Serial.print("menuHandlerCurveItemY - index: ");
    Serial.print(index);
    Serial.print(", value: ");
    Serial.print(maxDeltaHigh[index]);
    Serial.print(", diff: ");
    Serial.print(diff);
    Serial.println("");
    maxDeltaHigh[index] += diff;
    return &maxDeltaHigh[index];
}

void *menuHandlerCircuitRelayForced(void* param, int8_t diff) {
    if (diff != 0) {
        config.circuitRelayForced = (config.circuitRelayForced + 1) % 3;
    }
    return &config.circuitRelayForced;
}

void *menuHandlerServoMin(void* param, int8_t diff) {
    config.servoMin += int16_t(diff);
    return &config.servoMin;
}

void *menuHandlerServoMax(void* param, int8_t diff) {
    config.servoMax += int16_t(diff);
    return &config.servoMax;
}

void *menuHandlerOverheatingLimit(void* param, int8_t diff) {
    config.overheatingLimit += diff;
    return &config.overheatingLimit;
}

void *menuHandlerUnderheatingLimit(void* param, int8_t diff) {
    config.underheatingLimit += diff;
    return &config.underheatingLimit;
}

void menuFormatterUInt8Value(void* param, char *pBuffer, int16_t maxLen, void *value) {
    snprintf(pBuffer, maxLen, "value: %d", *(uint8_t*)value);
}

void menuFormatterInt16Value(void* param, char *pBuffer, int16_t maxLen, void *value) {
    snprintf(pBuffer, maxLen, "value: %d", *(int16_t*)value);
}

void menuFormatterInt8Value(void* param, char *pBuffer, int16_t maxLen, void *value) {
    snprintf(pBuffer, maxLen, "value: %d", *(int8_t*)value);
}

void menuFormatterFloatValue(void* param, char *pBuffer, int16_t maxLen, void *value) {
    dtostrf(*(float*)value, 3, 1, floatStrTemp);
    snprintf(pBuffer, maxLen, "value: %s", floatStrTemp);
}

void menuFormatterCircuitOverride(void* param, char *pBuffer, int16_t maxLen, void *value) {
    switch (*(int8_t*) value) {
        case 0:
            snprintf(pBuffer, maxLen, "no override");
            break;
        case 1:
            snprintf(pBuffer, maxLen, "always enabled");
            break;
        case 2:
            snprintf(pBuffer, maxLen, "always disabled");
            break;
    }
}

#define MENU_STATIC_ITEMS 10
ConfigMenuItem_t menu[] = {
        {
                .name = "Boiler Temp.",
                .param = nullptr,
                .handler = &menuHandlerBoiler,
                .formatter = &menuFormatterUInt8Value
        },
        {
                .name = "Room Temp.",
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
                .name = "[E] Overheating C",
                .param = nullptr,
                .handler = &menuHandlerOverheatingLimit,
                .formatter = &menuFormatterUInt8Value
        },
        {
                .name = "[E] Underheating C",
                .param = nullptr,
                .handler = &menuHandlerUnderheatingLimit,
                .formatter = &menuFormatterUInt8Value
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
struct ConfigMenuItem *getMenu(int16_t settingsSelected)
{
    if (settingsSelected < MENU_STATIC_ITEMS) {
        return &menu[settingsSelected];
    } else {
        uint16_t index = settingsSelected - MENU_STATIC_ITEMS;
        bool isY = (index % 2) == 1;
        uintptr_t i = index / 2;
        snprintf(bufferMenuName, 20, "[E] Curve[%d].%s", i, isY ? "%" : "dC");
        bufferMenuItem.name = bufferMenuName;
        bufferMenuItem.handler = isY ? &menuHandlerCurveItemY : &menuHandlerCurveItemX;
        bufferMenuItem.formatter = isY ? &menuFormatterUInt8Value : &menuFormatterInt8Value;
        bufferMenuItem.param = (void*) i;
        return &bufferMenuItem;
    }
}

uint8_t btn1[2] = {BTN_1_PIN, LOW};
uint8_t btn2[2] = {BTN_2_PIN, LOW};
uint8_t btn3[2] = {BTN_3_PIN, LOW};
uint8_t btn4[2] = {BTN_4_PIN, LOW};

void sendPulse(int positionPercent, int step) {
    // int time = 1500 + ((positionPercent - 50)*step);
    float multi = float(config.servoMax - config.servoMin) / 100;
    servo.write((100 - positionPercent + config.servoMin) * multi);
}

void printSettings() {
    lcd.cursor();

    ConfigMenuItem_t *currentItem = getMenu(settingsSelected);

    lcd.setCursor(0, 0);
    snprintf(buffer, 40, "%s ", currentItem->name);
    lcd.print(buffer);

    lcd.setCursor(0, 1);
    currentItem->formatter(currentItem->param, buffer, 40, currentItem->handler(currentItem->param, 0));
    lcd.print(buffer);
    lcd.setCursor(strlen(buffer), 1);
}

void printStatusOverview() {
    lcd.noCursor();
    lcd.setCursor(0, 0);
    ltoa(angle, floatStrTemp, 10);
    snprintf(buffer, 40, "O:%s%%  ", floatStrTemp);
    lcd.print(buffer);
    lcd.setCursor(7, 0);
    snprintf(
            buffer,
            40,
            "C:%c%c H:%c",
            config.circuitRelayForced != 0 ? 'O' : (
                    circuitRelay == 1 ? 'Y' : 'N'
            ),
            config.circuitRelayForced == 0 ? ' ' : (
                    config.circuitRelayForced == 1 ? 'Y' : 'N'
            ),
            heatNeeded ? 'Y' : 'N'
    );
    lcd.print(buffer);

    lcd.setCursor(0, 1);
    dtostrf(boilerTemp, 3, 1, floatStrTemp);
    snprintf(buffer, 40, "B:%sC  ", floatStrTemp);
    lcd.print(buffer);
    lcd.setCursor(9, 1);
    dtostrf(roomTemp, 2, 1, floatStrTemp);
    snprintf(buffer, 40, "R:%sC  ", floatStrTemp);
    lcd.print(buffer);
}

void printStatus() {
    lcd.clear();
    if (settingsSelected >= 0) {
        printSettings();
    } else {
        printStatusOverview();
    }
}

#define MAX_MENU_ITEMS (MENU_STATIC_ITEMS + (config.curveItems * 2) + 1)

bool processSettings() {
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
        ConfigMenuItem_t *currentItem = getMenu(settingsSelected);
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

void loop() {
    int stateChanged = processSettings();
    float lastBoilerTemp = boilerTemp;
    float lastRoomTemp = roomTemp;

    boilerTemp = readTemp(A0);
    roomTemp = readTemp(A1);

    int lastAngle = angle;
    angle = 100;

    heatNeeded = (heatNeeded && (roomTemp - config.refTempRoom <= (config.debounceLimitC / 2))) ||
                 (roomTemp - config.refTempRoom <= -(config.debounceLimitC / 2));

    int boilerDelta = boilerTemp - (heatNeeded ? config.refTempBoiler : config.refTempBoilerIdle);

    overheating = boilerTemp > config.overheatingLimit;
    underheating = boilerTemp < config.underheatingLimit;

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

    // override servo driving for adjusting purposes
    if (settingsSelected == 3) {
        angle = 100;
    } else if (settingsSelected == 4) {
        angle = 0;
    }

    bool inputsChanged = (angle != lastAngle) ||
                         (boilerTemp != lastBoilerTemp) ||
                         (roomTemp != lastRoomTemp);

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

    stateChanged |= inputsChanged;

    circuitRelay = !underheating && (heatNeeded || overheating);

    bool circuitRelayOrOverride = circuitRelay;
    if (config.circuitRelayForced != 0) {
        circuitRelayOrOverride = config.circuitRelayForced == 1;
    }

    // safety mechanism
    if (boilerTemp > 85) {
        angle = 0;
        circuitRelay = true;
    }

    sendPulse(angle, 20);

    digitalWrite(CIRCUIT_RELAY_PIN, circuitRelayOrOverride);

    if (stateChanged) {
        printStatus();
    }

    ticks += 1;

    delay(10);
}

#define EEPROM_MAGIC 0xDEADBEEF

void eepromInit() {
    uint32_t checkCode;
    EEPROM.get(0, checkCode);
    if (checkCode == EEPROM_MAGIC) {
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

void eepromUpdate() {
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
