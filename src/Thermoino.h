typedef struct ConfigMenuItem {
    const char *name;
    void* param;
    void* (*handler)(void* param, int8_t diff);
    void (*formatter)(void* param, char *buffer, int16_t maxLen, void *value);
} ConfigMenuItem_t;

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
    // linear interpolation (least squares) of the following points:
    // [boilerTemp - roomTemp, real boilerTemp - boilerTemp]
    float deltaTempPoly1;
    float deltaTempPoly0;
};

typedef struct Button {
    uint8_t pin;
    uint8_t *state;
} Button_t;