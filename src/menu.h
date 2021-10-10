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
