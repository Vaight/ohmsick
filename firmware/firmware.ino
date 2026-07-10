#include <EEPROM.h>           // todo - remove EEPROM in favor of vst3 serial assignment & storage
#define EEPROM_MAGIC 0xCAFE
#define EEPROM_VERSION 1

#define MAX_SIZE 8            // the maximum number of assigned pins per array

int pinsDigital[MAX_SIZE];    // array of assigned digital input pins
int pinsPullup[MAX_SIZE];     // array of assigned digital pullup resistor input pins
int pinsAnalog[MAX_SIZE];     // array of assigned analog input pins
int totalPins = 0;

struct SavedConfig {
  uint16_t magic;
  uint8_t version;
  int pinsDigital[MAX_SIZE];
  int pinsPullup[MAX_SIZE];
  int pinsAnalog[MAX_SIZE];
};

// executes on startup
void setup() {
  Serial.begin(19200);
  loadConfigFromEEPROM();

  applyPinModes();
  recalculateTotalPins();

  Serial.println("READY");
}

// loops while powered
void loop() {

  // check if a serial message exists
  if (Serial.available() > 0) {
    // get serial message as string
    String msg = Serial.readStringUntil('\n');
    msg.trim();
    // the "assignment" command: 'a <pin int> <action int>  
    if (msg.startsWith("a")) {
      // split and convert inputs to integers
      int pin = getValue(msg, ' ', 1).toInt();
      int action = getValue(msg, ' ', 2).toInt();
      // cases based on provided action type
      switch (action) {
        case 0: removeInput(pin);    // remove pin configuration
          break;
        case 1: setInput(pin);       // set pin to digital input
          break;
        case 2: setInputPullup(pin); // set pin to digital pullup input
          break;
        case 3: setInputAnalog(pin); // set pin to analog input
          break;
      }

      recalculateTotalPins();
      // saveto local config (remove later)
      saveConfigToEEPROM();
    }
  }

  // string builder variable
  String hardwareData;
  hardwareData += String(totalPins) + " ";

  // get pin string status for analog pins
  for (int pin : pinsAnalog) {
    if (pin != -1) hardwareData += getAnalogStatus(pin);
  }

  // get pin string status for digital pins
  for (int pin : pinsDigital) {
    if (pin != -1) hardwareData += getDigitalStatus(pin);
  }

  // get pin string status for digital pullup pins
  for (int pin : pinsPullup) {
    if (pin != -1) hardwareData += getPullupStatus(pin);
  }

  Serial.println(hardwareData);

  delay(20);
}

// ---- array helper functions ----------------------------------------------------

// 'erases' an assignment array given a pointer
void eraseArray(int* arr) {
  // iterate over array and set values to '-1'
  for (int i = 0; i < MAX_SIZE; i++) arr[i] = -1;
}

// returns the first index of a value in an array
int getIndexInArray(int* arr, int value) {
  int idx = -1;
  // iterate over array
  for (int i = 0; i < MAX_SIZE; i++) {
    if (arr[i] == value) {
      idx = i;
      break;
    }
  }
  // return result
  return idx;
}

// --------------------------------------------------------------------------------

// init config (remove later)
void initializeEmptyConfig() {
  eraseArray(pinsDigital);
  eraseArray(pinsPullup);
  eraseArray(pinsAnalog);
  totalPins = 0;
}

// load config (remove later)
void loadConfigFromEEPROM() {
  SavedConfig config;
  EEPROM.get(0, config);

  if (config.magic != EEPROM_MAGIC || config.version != EEPROM_VERSION) {
    initializeEmptyConfig();
    saveConfigToEEPROM();
    return;
  }

  for (int i = 0; i < MAX_SIZE; i++) {
    pinsDigital[i] = config.pinsDigital[i];
    pinsPullup[i] = config.pinsPullup[i];
    pinsAnalog[i] = config.pinsAnalog[i];
  }
}

// save to config (remove later)
void saveConfigToEEPROM() {
  SavedConfig config;

  config.magic = EEPROM_MAGIC;
  config.version = EEPROM_VERSION;

  for (int i = 0; i < MAX_SIZE; i++) {
    config.pinsDigital[i] = pinsDigital[i];
    config.pinsPullup[i] = pinsPullup[i];
    config.pinsAnalog[i] = pinsAnalog[i];
  }

  EEPROM.put(0, config);
}

// arduino pin mode stuff
void applyPinModes() {
  for (int pin : pinsDigital) {
    if (pin != -1) {
      pinMode(pin, INPUT);
    }
  }

  for (int pin : pinsPullup) {
    if (pin != -1) {
      pinMode(pin, INPUT_PULLUP);
    }
  }

  for (int pin : pinsAnalog) {
    if (pin != -1) {
      pinMode(pin, INPUT);
    }
  }
}

// clear config
void clearAssignments() {
  initializeEmptyConfig();
  recalculateTotalPins();
}

// get total number of pins
void recalculateTotalPins() {
  int tot = 0;

  for (int pin : pinsDigital) {
    if (pin != -1) tot++;
  }

  for (int pin : pinsPullup) {
    if (pin != -1) tot++;
  }

  for (int pin : pinsAnalog) {
    if (pin != -1) tot++;
  }

  totalPins = tot;
}

// set pin to digital input
void setInput(int pin) {
  removeInput(pin);

  int targetIdx = getIndexInArray(pinsDigital, -1);
  if (targetIdx == -1) return;

  pinsDigital[targetIdx] = pin;
  pinMode(pin, INPUT);
}

// set pin to digital pullup input
void setInputPullup(int pin) {
  removeInput(pin);

  int targetIdx = getIndexInArray(pinsPullup, -1);
  if (targetIdx == -1) return;

  pinsPullup[targetIdx] = pin;
  pinMode(pin, INPUT_PULLUP);
}

// set pin to analog input
void setInputAnalog(int pin) {
  removeInput(pin);

  int targetIdx = getIndexInArray(pinsAnalog, -1);
  if (targetIdx == -1) return;

  pinsAnalog[targetIdx] = pin;
  pinMode(pin, INPUT);
}

// unassign a pin from being an input
void removeInput(int pin) {
  // check arrays for pin
  int digitalIdx = getIndexInArray(pinsDigital, pin);
  int pullupIdx = getIndexInArray(pinsPullup, pin);
  int analogIdx = getIndexInArray(pinsAnalog, pin);
  // remove pin from arrays
  if (digitalIdx != -1) pinsDigital[digitalIdx] = -1;
  if (pullupIdx != -1) pinsPullup[pullupIdx] = -1;
  if (analogIdx != -1) pinsAnalog[analogIdx] = -1;
}

// returns formatted serial string for an analog pin
String getAnalogStatus(int pin) {
  int inp = analogRead(pin);
  inp = map(inp, 0, 1023, 0, 100);
  inp = constrain(inp, 0, 100);

  return String(pin) + "P" + intWithZeros(inp) + " ";
}

// returns formatted serial string for a digital pin
String getDigitalStatus(int pin) {
  int inp = digitalRead(pin);

  return String(pin) + "B" + String(inp) + " ";
}

// returns formatted serial string for a pullup pin
String getPullupStatus(int pin) {
  int inp = digitalRead(pin);

  return String(pin) + "B" + String(!inp) + " ";
}

// helper for adding zeros to int strings
String intWithZeros(int i) {
  String intStr = String(i);

  if (intStr.length() == 1) return "00" + intStr;
  else if (intStr.length() == 2) return "0" + intStr;

  return intStr;
}

// string splitter helper
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}
