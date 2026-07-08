#include <EEPROM.h>

#define MAX_SIZE 32

#define EEPROM_MAGIC 0xCAFE
#define EEPROM_VERSION 1

int btnPins[MAX_SIZE];
int potPins[MAX_SIZE];
int totPins = 0;
bool inSetupMode = false;

struct SavedConfig {
  uint16_t magic;
  uint8_t version;
  int btnPins[MAX_SIZE];
  int potPins[MAX_SIZE];
};

void setup() {
  Serial.begin(19200);

  loadConfigFromEEPROM();

  applyPinModes();
  recalculateTotalPins();

  Serial.println("READY");
}

void loop() {
  if (Serial.available() > 0) {
    String message = Serial.readStringUntil('\n');
    message.trim();

    if (message == "setup") {
      Serial.println("stop-data");
      Serial.println("YOU ARE NOW IN SETUP MODE. SEND 'help' FOR HELP.");
      inSetupMode = true;
    }

    if (inSetupMode) {
      if (message == "exit") {
        saveConfigToEEPROM();
        Serial.println("Configuration saved.");
        Serial.println("start-data");
        inSetupMode = false;
        return;
      }
      else if (message == "help") {
        Serial.println("HELP MENU:");
        Serial.println("help - show this menu");
        Serial.println("exit - close setup mode and save");
        Serial.println("assign <pin#> <P/B/N> - assign a pin");
        Serial.println("save - save current assignments");
        Serial.println("clear - erase all assignments");
        Serial.println("dump - show all assignments");
      }
      else if (message.startsWith("assign")) {
        String part2 = getValue(message, ' ', 1);
        String part3 = getValue(message, ' ', 2);

        part3.toUpperCase();

        if (part2 == "" || part3 == "") {
          Serial.println("assign <pin#> <P/B/N> - assign a pin");
        } else {
          int pin = part2.toInt();

          Serial.println("Pin to assign: " + String(pin));
          Serial.println("Assignment: " + part3);

          if (part3 == "B") {
            pinMode(pin, INPUT_PULLUP);
            addBtn(pin);
            Serial.println("pin assigned to button!");
          }
          else if (part3 == "P") {
            pinMode(pin, INPUT);
            addPot(pin);
            Serial.println("pin assigned to potentiometer!");
          }
          else if (part3 == "N") {
            removeBtn(pin);
            removePot(pin);
            Serial.println("pin unassigned!");
          }
          else {
            Serial.println("Invalid assignment. Use B, P, or N.");
          }

          recalculateTotalPins();
        }
      }
      else if (message == "save") {
        saveConfigToEEPROM();
        Serial.println("Configuration saved.");
      }
      else if (message == "clear") {
        clearAssignments();
        saveConfigToEEPROM();
        Serial.println("Assignments cleared and saved.");
      }
      else if (message.startsWith("dump")) {
        Serial.println("BUTTON PINS:");
        for (int i = 0; i < MAX_SIZE; i++) {
          Serial.println("btn" + String(i) + ": " + String(btnPins[i]));
        }

        Serial.println("POT PINS:");
        for (int i = 0; i < MAX_SIZE; i++) {
          Serial.println("pot" + String(i) + ": " + String(potPins[i]));
        }

        Serial.println("Total assigned pins: " + String(totPins));
      }
    }
  }

  if (inSetupMode == true) return;

  String hardwareData;
  hardwareData += String(totPins) + " ";

  for (int pin : potPins) {
    if (pin != -1) hardwareData += getPotPinStatus(pin);
  }

  for (int pin : btnPins) {
    if (pin != -1) hardwareData += getBtnPinStatus(pin);
  }

  Serial.println(hardwareData);

  delay(20);
}

void initializeEmptyConfig() {
  for (int i = 0; i < MAX_SIZE; i++) {
    btnPins[i] = -1;
    potPins[i] = -1;
  }

  totPins = 0;
}

void loadConfigFromEEPROM() {
  SavedConfig config;
  EEPROM.get(0, config);

  if (config.magic != EEPROM_MAGIC || config.version != EEPROM_VERSION) {
    initializeEmptyConfig();
    saveConfigToEEPROM();
    return;
  }

  for (int i = 0; i < MAX_SIZE; i++) {
    btnPins[i] = config.btnPins[i];
    potPins[i] = config.potPins[i];
  }
}

void saveConfigToEEPROM() {
  SavedConfig config;

  config.magic = EEPROM_MAGIC;
  config.version = EEPROM_VERSION;

  for (int i = 0; i < MAX_SIZE; i++) {
    config.btnPins[i] = btnPins[i];
    config.potPins[i] = potPins[i];
  }

  EEPROM.put(0, config);
}

void applyPinModes() {
  for (int pin : btnPins) {
    if (pin != -1) {
      pinMode(pin, INPUT_PULLUP);
    }
  }

  for (int pin : potPins) {
    if (pin != -1) {
      pinMode(pin, INPUT);
    }
  }
}

void clearAssignments() {
  initializeEmptyConfig();
  recalculateTotalPins();
}

void recalculateTotalPins() {
  int tot = 0;

  for (int pin : btnPins) {
    if (pin != -1) tot++;
  }

  for (int pin : potPins) {
    if (pin != -1) tot++;
  }

  totPins = tot;
}

void addBtn(int i) {
  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (btnPins[idx] == i) return;
  }

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (potPins[idx] == i) {
      removePot(i);
      break;
    }
  }

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (btnPins[idx] == -1) {
      btnPins[idx] = i;
      break;
    }
  }
}

void removeBtn(int i) {
  int targetIdx = -1;

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (btnPins[idx] == i) {
      targetIdx = idx;
      break;
    }
  }

  if (targetIdx != -1) {
    for (int idx = targetIdx; idx < MAX_SIZE - 1; idx++) {
      btnPins[idx] = btnPins[idx + 1];
    }

    btnPins[MAX_SIZE - 1] = -1;
  }
}

void addPot(int i) {
  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (potPins[idx] == i) return;
  }

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (btnPins[idx] == i) {
      removeBtn(i);
      break;
    }
  }

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (potPins[idx] == -1) {
      potPins[idx] = i;
      break;
    }
  }
}

void removePot(int i) {
  int targetIdx = -1;

  for (int idx = 0; idx < MAX_SIZE; idx++) {
    if (potPins[idx] == i) {
      targetIdx = idx;
      break;
    }
  }

  if (targetIdx != -1) {
    for (int idx = targetIdx; idx < MAX_SIZE - 1; idx++) {
      potPins[idx] = potPins[idx + 1];
    }

    potPins[MAX_SIZE - 1] = -1;
  }
}

String getPotPinStatus(int pin) {
  int inp = analogRead(pin);
  inp = map(inp, 0, 1023, 0, 100);
  inp = constrain(inp, 0, 100);

  return String(pin) + "P" + intWithZeros(inp) + " ";
}

String getBtnPinStatus(int pin) {
  int inp = digitalRead(pin);

  return String(pin) + "B" + String(!inp) + " ";
}

String intWithZeros(int i) {
  String intStr = String(i);

  if (intStr.length() == 1) return "00" + intStr;
  else if (intStr.length() == 2) return "0" + intStr;

  return intStr;
}

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