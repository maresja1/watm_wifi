#include <EEPROM.h>

#include <Servo.h> 

// include the library code:
#include <LiquidCrystal.h>

Servo myservo;

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
#define MAX_DELTA_SETTINGS 5
const int maxDeltaSettings[MAX_DELTA_SETTINGS] = {-10, -5, 0, 5, 10};
const int maxDeltaHigh[MAX_DELTA_SETTINGS] = {75, 50, 25, 12, 0};

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(13, 12, 11, 10, 9, 8);

void setup() {
  myservo.attach(SERVO_PIN);
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
}

float readTemp(int port) {
  int mV = analogRead(port);
  return ((mV - minV) / resTemp) + minTemp;
  
}

int8_t readButton(uint8_t button[2])
{
  int state = 0;
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

struct Configuration{
  uint8_t refTempBoiler;
  float refTempRoom;
  uint8_t circuitRelayForced;
  int16_t servoMin;
  int16_t servoMax;
};

Configuration config = {
  .refTempBoiler = 60,
  .refTempRoom = 22.0f,
  .circuitRelayForced = 0,
  .servoMin = 0,
  .servoMax = 180
};

uint32_t ticks = 0;
uint8_t angle = 100;
uint8_t settingsMode = 0;
uint8_t settingsSelected = 0;
char buffer[40];
char floatStrTemp[6];
float boilerTemp = 0.0f;
float roomTemp = 0.0f;
bool circuitRelay = false;

uint8_t btn1[2] = {BTN_1_PIN, LOW};
uint8_t btn2[2] = {BTN_2_PIN, LOW};
uint8_t btn3[2] = {BTN_3_PIN, LOW};
uint8_t btn4[2] = {BTN_4_PIN, LOW};

void sendPulse(int positionPercent, int step) {
  // int time = 1500 + ((positionPercent - 50)*step);
  float multi = float(config.servoMax - config.servoMin) / 100;
  myservo.write((100 - positionPercent + config.servoMin) * multi);
}

void printSettings()
{
  lcd.cursor();
  
  lcd.setCursor(0, 0);
  ltoa(settingsSelected, floatStrTemp, 10);
  snprintf(buffer, 40, "P:%s  ", floatStrTemp);
  lcd.print(buffer);
  
  if (settingsSelected <= 1) {
    lcd.setCursor(0, 1);  
    ltoa(config.refTempBoiler, floatStrTemp, 10);
    snprintf(buffer, 40, "rK:%sC  ", floatStrTemp);
    lcd.print(buffer);
    lcd.setCursor(9, 1);
    dtostrf(config.refTempRoom, 3, 1, floatStrTemp);
    snprintf(buffer, 40, "rP:%sC  ", floatStrTemp);
    lcd.print(buffer);
  } else if (settingsSelected <= 2) {
    lcd.setCursor(0, 1);  
    snprintf(
      buffer,
      40,
      "CO:%c ",
      config.circuitRelayForced == 0 ? '-' : (
        config.circuitRelayForced == 1 ? 'A' : 'N'
      )
    );
    lcd.print(buffer);    
  } else if (settingsSelected <= 4) {
    lcd.setCursor(0, 1);  
    ltoa(config.servoMin, floatStrTemp, 10);
    snprintf(buffer, 40, "Smn:%s ", floatStrTemp);
    lcd.print(buffer);
    lcd.setCursor(9, 1);
    dtostrf(config.servoMax, 3, 1, floatStrTemp);
    snprintf(buffer, 40, "Smx:%s ", floatStrTemp);
    lcd.print(buffer);
  }
  switch(settingsSelected) {
    case 0:
  	  lcd.setCursor(3, 1);    
      break;
    case 1:
  	  lcd.setCursor(12, 1);    
      break;
    case 2:
  	  lcd.setCursor(3, 1);  
      break;
    case 3:
  	  lcd.setCursor(4, 1);    
      break;
    case 4:
  	  lcd.setCursor(13, 1);    
      break;
  }
}

void printStatusOverview()
{  
  lcd.noCursor();
  lcd.setCursor(0, 0);
  ltoa(angle, floatStrTemp, 10);
  snprintf(buffer, 40, "O:%s  ", floatStrTemp);
  lcd.print(buffer);
  lcd.setCursor(7, 0);
  snprintf(
    buffer,
    40,
    "C:%c%c ",
    config.circuitRelayForced != 0 ? 'O' : (
      circuitRelay == 1 ? 'A' : 'N'
    ),
    config.circuitRelayForced == 0 ? ' ' : (
      config.circuitRelayForced == 1 ? 'A' : 'N'
    )
  );
  lcd.print(buffer);
  
  lcd.setCursor(0, 1);  
  dtostrf(boilerTemp, 3, 1, floatStrTemp);
  snprintf(buffer, 40, "K:%sC  ", floatStrTemp);
  lcd.print(buffer);
  lcd.setCursor(9, 1);
  dtostrf(roomTemp, 2, 1, floatStrTemp);
  snprintf(buffer, 40, "P:%sC  ", floatStrTemp);
  lcd.print(buffer);
}

void printStatus() {
  lcd.setCursor(13, 0);
  ltoa(settingsMode, floatStrTemp, 10);
  snprintf(
    buffer,
    40,
    "S:%s",
    floatStrTemp
  );
  lcd.print(buffer);
  if (settingsMode == 1) {
    printSettings();
  } else {
    printStatusOverview();
  }
}

bool processSettings()
{  
  bool stateChanged = false;
  if (readButton(btn1) == 1) {
    stateChanged = true;
    settingsMode = (settingsMode + 1) % 3;
    lcd.clear();
  }
  if (settingsMode == 1) {
    if (readButton(btn2) == 1) {
      stateChanged = true;
      settingsSelected = (settingsSelected + 1) % 5;
      lcd.clear();
    }
    if (readButton(btn3) == 1) {
      stateChanged = true;
      switch(settingsSelected) {
        case 0:
          config.refTempBoiler--;
          break;
        case 1:
          config.refTempRoom -= 0.2f;
          break;
        case 2:
          config.circuitRelayForced = (config.circuitRelayForced + 1) % 3;
          break;
        case 3:
          config.servoMin--;
          break;
        case 4:
          config.servoMax--;
          break;
      }
    }
    if (readButton(btn4) == 1) {
      stateChanged = true;
      switch(settingsSelected) {
        case 0:
          config.refTempBoiler++;
          break;
        case 1:
          config.refTempRoom += 0.2f;
          break;
        case 2:
          config.circuitRelayForced = (config.circuitRelayForced + 1) % 3;
          break;
        case 3:
          config.servoMin++;
          break;
        case 4:
          config.servoMax++;
          break;
      }
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
  
  int boilerDelta = boilerTemp - config.refTempBoiler;
  
  bool overheating = (boilerDelta >= maxDeltaSettings[MAX_DELTA_SETTINGS-1]);
  bool underheating = (boilerDelta <= maxDeltaSettings[0]);
  bool heatNeeded = (roomTemp - config.refTempRoom <= 0);
  
  for (int i = MAX_DELTA_SETTINGS - 1; i >= 0; i--) {
    if (boilerDelta >= maxDeltaSettings[i]) {
      int nextAngle = maxDeltaHigh[i];
      int nextI = i + 1;
      if (nextI < MAX_DELTA_SETTINGS) {
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
      angle = nextAngle / ((heatNeeded || underheating) ? 1 : 2);
      break;
    }
  }
  
  // override servo driving for adjusting purposes
  if (settingsMode == 1 && settingsSelected == 3) {
    angle = 100;
  } else if (settingsMode == 1 && settingsSelected == 4) {
    angle = 0;
  }
  
  stateChanged |= (angle != lastAngle) || 
    (boilerTemp != lastBoilerTemp) || 
    (roomTemp != lastRoomTemp);
    
  circuitRelay = !underheating && (heatNeeded || overheating);
  
  bool circuitRelayOrOverride = circuitRelay;
  if (config.circuitRelayForced != 0) {
    circuitRelayOrOverride = config.circuitRelayForced == 1;
  }
  
  sendPulse(angle, 20);
  
  digitalWrite(CIRCUIT_RELAY_PIN, circuitRelayOrOverride);
  
  if (stateChanged) {
    printStatus();
  }
      
  ticks += 1;
  
  delay(10);
}

void eepromInit()
{
  uint32_t checkCode;
  EEPROM.get(0, checkCode);
  if (checkCode == 0xDEADBEEF) {
    EEPROM.get(sizeof(checkCode), config);
  } else {
    checkCode = 0xDEADBEEF;
    EEPROM.put(0, checkCode);
    EEPROM.put(sizeof(checkCode), config);
  }
}

void eepromUpdate()
{
   EEPROM.put(sizeof(uint32_t), config);
}