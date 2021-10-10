#include <Arduino.h>

#include "Thermoino.h"

#include <EEPROM.h>
#if USE_DHT_ROOM_TEMP
#include <DHT.h>
#include <DHT_U.h>
#endif
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <TaskScheduler.h>


void notifyTask(Task *task, bool immediate);

void stateUpdate_readSensors_cb();
void effect_refreshServoAndRelay_cb();
void effect_printStatus_cb();
void stateUpdate_angleAndRelay_cb();
void stateUpdate_readButtons_cb();

Scheduler runner;
//Tasks
Task t_stateUpdate_readButtons(100, -1, &stateUpdate_readButtons_cb, &runner);
Task t_stateUpdate_angleAndRelay(1000, 1, &stateUpdate_angleAndRelay_cb, &runner);
Task t_stateUpdate_readSensors(2000, -1, &stateUpdate_readSensors_cb, &runner);
Task t_effect_refreshServoAndRelay(6000, 1, &effect_refreshServoAndRelay_cb, &runner);
Task t_effect_printStatus(1000, 1, &effect_printStatus_cb, &runner);

// initialize the library with the numbers of the interface pins
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);

#if USE_DHT_ROOM_TEMP
// instance of digital thermometer
DHT_Unified digitalThermometer(DHT21_PIN, DHT21);
#endif

// servo allowing air to come in the boiler
Servo servo;

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
    .deltaTempPoly1 = 0.0f, //0.2333f,
    .deltaTempPoly0 = 0.0f//1.612f
};

#define MAX_BUFFER_LEN 20
char buffer[20];

uint8_t angle = 99;
int16_t settingsSelected = -1;
float boilerTemp = 0.0f;
float roomTemp = 0.0f;
float roomHumidity = 0.0f;
bool heatNeeded = false;
bool overheating = false;
bool underheating = false;
bool circuitRelay = false;

void setup()
{
    Serial.begin(9600);

    for (int i = 0; i < 10 && !Serial; ++i) {
        // wait for serial port to connect. Needed for native USB port only
        delay(10);
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

    // set up DHT
#if USE_DHT_ROOM_TEMP
    digitalThermometer.begin();
#endif

    delay(1000);

    eepromInit();
    Serial.println("Thermoino 1, built:" __DATE__ " " __TIME__ " (" __FILE__ ") - Setup finished.");
    t_stateUpdate_readButtons.enable();
    t_stateUpdate_readSensors.enable();
    t_stateUpdate_angleAndRelay.enable();
    t_effect_refreshServoAndRelay.enable();
    t_effect_printStatus.enable();
}

void loop()
{
    runner.execute();
}

void stateUpdate_readSensors_cb()
{
    const float lastBoilerTemp = boilerTemp;
    const float lastRoomTemp = roomTemp;

    boilerTemp = readTemp(BOILER_THERM_PIN);

#if USE_DHT_ROOM_TEMP
    sensors_event_t event;
    digitalThermometer.temperature().getEvent(&event);
    roomTemp = event.temperature;

    digitalThermometer.humidity().getEvent(&event);
    roomHumidity = event.relative_humidity;
#else
    roomTemp = readTemp(ROOM_THERM_PIN);
#endif

    // roomTemp is not NaN
    if (roomTemp == roomTemp) {
        boilerTemp += (config.deltaTempPoly1 * (boilerTemp - roomTemp)) + config.deltaTempPoly0;
    }

    if((boilerTemp != lastBoilerTemp) || (roomTemp != lastRoomTemp)) {
        notifyTask(&t_stateUpdate_angleAndRelay, false);
        notifyTask(&t_effect_printStatus, false);

#if DEBUG_LEVEL > 1
        DEBUG_SER_PRINT(boilerTemp);
        DEBUG_SER_PRINT_LN(roomTemp);
#endif
    }
}

void stateUpdate_angleAndRelay_cb()
{
    heatNeeded = (heatNeeded && (roomTemp - config.refTempRoom <= (config.debounceLimitC / 2))) ||
                 (roomTemp - config.refTempRoom <= -(config.debounceLimitC / 2));

    const float boilerDelta = boilerTemp - float(heatNeeded ? config.refTempBoiler : config.refTempBoilerIdle);

    overheating = (overheating && (boilerTemp - config.overheatingLimit >= (config.debounceLimitC / 2))) ||
                  (boilerTemp - config.overheatingLimit >= -(config.debounceLimitC / 2));

    underheating = (underheating && (boilerTemp - config.underheatingLimit <= (config.debounceLimitC / 2))) ||
                   (boilerTemp - config.underheatingLimit <= -(config.debounceLimitC / 2));

    bool lastCircuitRelay = circuitRelay;
    circuitRelay = !underheating && (heatNeeded || overheating);

    uint8_t lastAngle = angle;
    angle = 99;

    for (int i = config.curveItems - 1; i >= 0; i--) {
        if (boilerDelta >= maxDeltaSettings[i]) {
            int nextAngle = maxDeltaHigh[i];
            int nextI = i + 1;
            if (nextI < config.curveItems) {
                // linear interpolation
                nextAngle = float(maxDeltaHigh[i]) +
                    (
                        (boilerDelta - float(maxDeltaSettings[i])) *
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

    if (lastAngle != angle || lastCircuitRelay != circuitRelay) {
        notifyTask(&t_effect_refreshServoAndRelay, false);
        notifyTask(&t_effect_printStatus, true);
    }
}

void effect_refreshServoAndRelay_cb()
{
    const bool circuitRelayOrOverride = config.circuitRelayForced == 0 ? circuitRelay : config.circuitRelayForced == 1;
    if (settingsSelected == 3) {
        servoSetPos(100);
        sendCurrentStateToRelay(circuitRelayOrOverride);
    } else if (settingsSelected == 4) {
        servoSetPos(0);
        sendCurrentStateToRelay(circuitRelayOrOverride);
    } else if (boilerTemp > 85) {
        // safety mechanism
        servoSetPos(0);
        sendCurrentStateToRelay(true);
    } else {
        servoSetPos(angle);
        sendCurrentStateToRelay(circuitRelayOrOverride);
    }
}

void effect_printStatus_cb()
{
    printStatus();
#if DEBUG_LEVEL > 1
    DEBUG_SER_PRINT(boilerTemp);
    DEBUG_SER_PRINT(roomTemp);
    DEBUG_SER_PRINT(angle);
    DEBUG_SER_PRINT(overheating);
    DEBUG_SER_PRINT_LN(underheating);
#endif
}

void stateUpdate_readButtons_cb()
{
    if (processSettings()) {
        notifyTask(&t_stateUpdate_angleAndRelay, true);
        notifyTask(&t_effect_refreshServoAndRelay, true);
        notifyTask(&t_effect_printStatus, true);
    }
}

void notifyTask(Task *task, bool immediate)
{
    task->setIterations(1);
    if (immediate) {
        task->enable();
    } else if (!task->isEnabled()) {
        task->enableDelayed(task->getInterval());
    }
}

void sendCurrentStateToRelay(const bool state) {
    digitalWrite(CIRCUIT_RELAY_PIN, !state);
}


// (1/T_0); T_0 = 25 C => 1 / (25 + 237.15) => 0.00381461f
#define THERM_RESIST_SERIAL 10000
#define THERM_REF_TEMP_INV 0.003354016f
#define THERM_REF_RESIST 10000
#define THERM_BETA 3977

float readTemp(uint8_t pin)
{
    analogRead(pin);
    delay(10);
    int V_out = analogRead(pin);
    // R_1 = R_2 * ( V_in / V_out - 1 ); V_in = 1023 (3.3V after conv.), R_2 = #THERM_RESIST_SERIAL
    float R_1 = (float(THERM_RESIST_SERIAL) * float(V_out)) / float (1023 - V_out);
#if DEBUG_LEVEL > 2
    DEBUG_SER_PRINT(V_out);
    DEBUG_SER_PRINT(R_1);
    DEBUG_SER_PRINT_LN(THERM_RESIST_SERIAL);
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

#include "menu.h"

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
    snprintf(buffer, MAX_BUFFER_LEN, "O %2d%%  ", angle);
    lcd.print(buffer);
    lcd.setCursor(6, 0);
    snprintf(
        buffer,
        MAX_BUFFER_LEN,
        "B %4.1f\xDF%c%c%c",
        (double) boilerTemp,
        config.circuitRelayForced == 0 ? ' ' : '>',
        config.circuitRelayForced != 0 ? yesOrNo(config.circuitRelayForced == 1) : yesOrNo(circuitRelay),
        yesOrNo(heatNeeded)
    );
    lcd.print(buffer);
}

void printStatusOverviewBottom()
{
    lcd.noCursor();
    lcd.setCursor(0, 1);
    // 2 + 4 + 2 = 8 chars
    snprintf(buffer, MAX_BUFFER_LEN, "H %2d%% ", (int) roomHumidity);
    lcd.print(buffer);
    lcd.setCursor(6, 1);
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

#define MAX_MENU_ITEMS (MENU_STATIC_ITEMS + (config.curveItems * 2) + 1)

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