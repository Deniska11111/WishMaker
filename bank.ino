#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <BluetoothSerial.h>
#include <EEPROM.h>

#define OBSCURE_INTERRUPT   33
#define UNOBSCURE_INTERRUPT 32
#define SERVO_PIN           10

// #define RESET

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lock;
BluetoothSerial bt;

// SETTINGS

char* ssid;
char* pass;

uint16_t goal = 0;
char* goal_title = "";

bool locked = false;

// EEPROM

const uint8_t ssid_addr = 0;
const uint8_t ssid_size = 32;
const uint8_t pass_addr = ssid_addr + ssid_size;
const uint8_t pass_size = 32;
const uint8_t goal_addr = pass_addr + pass_size;
const uint8_t goal_size = 2;
const uint8_t goal_title_addr = goal_addr + goal_size;
const uint8_t goal_title_size = 16;
const uint8_t locked_addr = goal_title_addr + goal_title_size;
const uint8_t locked_size = 1;
const uint8_t accum_addr = locked_addr + locked_size;
const uint8_t accum_size = 2;

////

uint16_t accumulated = 0;
double course = 43.0;

////

void saveSSID() {
  for(int i = 0; i < ssid_size; i++) {
    EEPROM.write(ssid_addr + i, ssid[i]);
  }
}

void savePass() {
  for(int i = 0; i < pass_size; i++) {
    EEPROM.write(pass_addr + i, pass[i]);
  }
  EEPROM.commit();
}

void saveGoal() {
  EEPROM.write(goal_addr, goal & 0xFF);
  EEPROM.write(goal_addr + 1, goal >> 8);
  EEPROM.commit();
}

void saveGoalTitle() {
  for(int i = 0; i < goal_title_size; i++) {
    EEPROM.write(goal_title_size + i, goal_title[i]);
  }
  EEPROM.commit();
}

void saveLock() {
  EEPROM.write(locked_addr, locked);
  EEPROM.commit();
}

void saveAccum() {
  EEPROM.write(accum_addr, accumulated & 0xFF);
  EEPROM.write(accum_addr + 1, accumulated >> 8);
  EEPROM.commit();
}

void EEPROMload() {
  for(int i = 0; i < ssid_size; i++) ssid[i] = EEPROM.read(ssid_addr + i);
  for(int i = 0; i < pass_size; i++) pass[i] = EEPROM.read(pass_addr + i);
  for(int i = 0; i < goal_title_size; i++) goal_title[i] = EEPROM.read(goal_title_addr + i);

  goal = EEPROM.read(goal_addr) | (EEPROM.read(goal_addr + 1) << 8);
  accumulated = EEPROM.read(accum_addr) | (EEPROM.read(accum_addr + 1) << 8);

  locked = EEPROM.read(locked_addr);
}

void EEPROMrst() {
  EEPROM.write(ssid_addr, 255);
  EEPROM.write(goal_addr, 0);
  EEPROM.write(goal_addr + 1, 0);
  EEPROM.write(accum_addr, 0);
  EEPROM.write(accum_addr + 1, 0);
  EEPROM.commit();
}

////

volatile bool needsUpdate = true;
void updateDisplay() {
  lcd.clear();
  lcd.print(accumulated);
  lcd.print(" grn");
  lcd.setCursor(0, 1);
  lcd.print(accumulated * 1.0 / course);
  lcd.print("$");

  if(goal > 0) {
    delay(2000);
    lcd.clear();
    lcd.print(goal_title);
    lcd.setCursor(0, 1);
    lcd.print("Progress: ");
    lcd.print((int) (accumulated * 1.0 / goal));
    lcd.print("%");
  }
}

///

volatile uint32_t lastObscuranceTime = 0;

void laserObscured() {
    lastObscuranceTime = micros();
}

void laserUnobscured() {
    uint32_t time_diff = micros() - lastObscuranceTime;

    Serial.println(time_diff);

    if(time_diff > 110000) {
      accumulated += 10;
    }
    else 
    if(time_diff > 80000) {
      accumulated += 5;
    }
    else 
    if(time_diff > 62000) {
      accumulated += 2;
    }
    else 
    if(time_diff > 45000) {
      accumulated += 1;
    }

    saveAccum();

    needsUpdate = true;
}

////

const char* server = "https://cdn.jsdelivr.net/npm/@fawazahmed0/currency-api@latest/v1/currencies/usd.json";
unsigned long lastTime = 0;
unsigned long timerDelay = 3600000;

void setup() {
  ssid = (char*) malloc(ssid_size);
  pass = (char*) malloc(pass_size);
  goal_title = (char*) malloc(goal_title_size);

  Serial.begin(9600);

  pinMode(OBSCURE_INTERRUPT, INPUT_PULLUP);
  pinMode(UNOBSCURE_INTERRUPT, INPUT_PULLUP);

  attachInterrupt(OBSCURE_INTERRUPT, laserObscured, FALLING);
  attachInterrupt(UNOBSCURE_INTERRUPT, laserUnobscured, RISING);

  EEPROM.begin(512);
  #ifdef RESET
    EEPROMrst();
    return;
  #endif

  lcd.begin();
  lock.attach(SERVO_PIN);
  bt.begin("Bank");

  EEPROMload();

  Serial.println();
  Serial.println(ssid);
  Serial.println(pass);
  Serial.println(goal);
  Serial.println(accumulated);

  lock.write(locked * 90);
  
  Serial.println((int) ssid[0]);

  if(ssid[0] == 255) {
    lcd.print("Setup me...");
  }
  else {
    WiFi.begin(ssid, pass);
    lcd.print("Connecting");
    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED) {
      delay(500);
      lcd.print(".");
      if(attempts++ > 10) {
        lcd.clear();
        lcd.print("WIFI Error!");
        break;
      }
    }
    lcd.clear();
    lcd.print("Connected!");
  }
  lastTime = -timerDelay + 5000;
}

void loop() {
  #ifdef RESET
  return;
  #endif
  if(millis() - lastTime > timerDelay) {
    if(WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      http.begin(server);

      int code = http.GET();
      if (code > 0) {
        String payload = http.getString();
        
        JsonDocument json;
        deserializeJson(json, payload);
        course = (double) json["usd"]["uah"];

      }
      else {
        Serial.println("Error");
        Serial.println(code);
      }

      http.end();
    }
    else {
      Serial.println("WIFI not connected!");
    }
    lastTime = millis();
  }

  if(bt.available()) {
    if(bt.read() == 2) {
      uint8_t data_type = bt.read();

      if(data_type == 'S') {
        for(int i = 0; i < 32 && bt.available(); i++) {
          int data = bt.read();
          ssid[i] = data;
          if(data == 0) break;
        }
        saveSSID();
        Serial.println("SSID updated!");
        Serial.println(ssid);
      }
      else if(data_type == 'P') {
        for(int i = 0; i < 32 && bt.available(); i++) {
          int data = bt.read();
          pass[i] = data;
          if(data == 0) break;
        }
        savePass();
        Serial.println("Pass updated!");
        Serial.println(pass);
      }
      else if(data_type == 'T') {
        for(int i = 0; i < 32 && bt.available(); i++) {
          int data = bt.read();
          goal_title[i] = data;
          if(data == 0) break;
        }
        saveGoalTitle();
        Serial.println("Goal title updated!");
        Serial.println(goal_title);
      }
      else if(data_type == 'G') {
        goal = bt.read();
        goal <<= 8;
        goal |= bt.read();
        saveGoal();
        Serial.println("Goal updated!");
        Serial.println(goal);
      }
      else if(data_type == 'L') {
        locked = bt.read();
        lock.write(locked * 90);
        saveLock();
        Serial.println(locked ? "Locked" : "Unlocked");
      }
    }
  }

  if(needsUpdate) { 
    needsUpdate = false;
    updateDisplay();
  }
}
