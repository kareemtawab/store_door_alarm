#include <DS3232RTC.h>      // https://github.com/JChristensen/DS3232RTC
#include <TimeLib.h>
#include "Timer.h"
#include <SoftwareSerial.h>
#include <GSMSimSMS.h>
#include <EEPROMex.h>
#include "Arduino.h"

#define SIM800LRX  4
#define SIM800LTX  3
#define REED_Pin 2
#define RTC_Pin LED_BUILTIN
#define SIM800LRST_PIN 10 // you can use any pin.

DS3232RTC myRTC;
Timer t;
SoftwareSerial SIM800Lserial(SIM800LRX, SIM800LTX); // RX, TX
GSMSimSMS SIM800L(SIM800Lserial, SIM800LRST_PIN); // SIM800LSimSMS inherit from SIM800LSim. You can use SIM800LSim methods with it.

bool isDoorOPEN, wasDoorOPEN;
bool sanitycheckrequired;
char* PhoneNo1 = "00201015322756";
char* PhoneNo2 = "00201002653950";
int sanitycheckrequiredaddress = 2;
int systemSentSMScounteraddress = 4;
int systemLastTimestampaddress = 6;
int systemLastsanitycheckTimestampaddress = 10;
int systemSentSMScounter;
String systemInboxSMScount;
int SIM800Linitduration;
int SIM800LStatus;
int systemTemperature;
unsigned long systemTimestamp;
unsigned long systemLastTimestamp;
unsigned long systemLastsanitycheckTimestamp;
unsigned long timesincelastSanitycheck;
unsigned long sanitycheckSMSinterval = 1 * (30 * 24 * 60 * 60); // sanity check SMS interval in seconds
unsigned long checkDoorInterval = 5 * (60); // system check interval in seconds

void setup() {
  Serial.begin(115200);
  EEPROM.setMemPool(0, 64);
  sanitycheckrequired = EEPROM.readInt(sanitycheckrequiredaddress);
  systemSentSMScounter = EEPROM.readInt(systemSentSMScounteraddress);
  systemLastTimestamp = EEPROM.readLong(systemLastTimestampaddress);
  systemLastsanitycheckTimestamp = EEPROM.readLong(systemLastsanitycheckTimestampaddress);
  SIM800Lserial.begin(9600);
  SIM800L.init();
  pinMode(REED_Pin, INPUT_PULLUP);
  pinMode(RTC_Pin, OUTPUT);
  digitalWrite(RTC_Pin, HIGH);
  myRTC.begin();
  setSyncProvider(myRTC.get);   // the function to get the time from the RTC
  t.every(250, getDoorState);
  t.every(checkDoorInterval * 1000, intervalCheckSystemState);
  t.every(10000, checkIncomingCall);
  t.every(1000, printSystemState);
  intro();
  systemTimestamp = now();
  //clearEEPROMvalues();
}

void loop() {
  /*
    while (Serial.available()) {
    SIM800Lserial.write(Serial.read());
    }
    while (SIM800Lserial.available()) {
    SIM800Lserial.write(SIM800Lserial.read());
    }
  */
  systemTemperature = myRTC.temperature() / 4;
  systemTimestamp = now();
  timesincelastSanitycheck = abs(systemTimestamp - systemLastsanitycheckTimestamp);
  if (timesincelastSanitycheck > sanitycheckSMSinterval) {
    sanitycheckrequired = true;
  }
  t.update();
}

void getDoorState() {
  isDoorOPEN = digitalRead(REED_Pin);
  if (isDoorOPEN != wasDoorOPEN) {
    if (isDoorOPEN == LOW) {
      if (SIM800LStatus == 0) {
        sendtoSIM800L_ondoorCLOSE();
      }
    }
    else {
      if (SIM800LStatus == 0) {
        sendtoSIM800L_ondoorOPEN();
      }
    }
  }
  wasDoorOPEN = isDoorOPEN;
}

void intervalCheckSystemState() {
  if (isDoorOPEN) {
    if (SIM800LStatus == 0) {
      sendtoSIM800L_onintervalwhileOPEN();
    }
  }
  if (sanitycheckrequired) {
    if (SIM800LStatus == 0) {
      sendtoSIM800L_onsanitycheck();
    }
  }
}

void checkIncomingCall() {
  SIM800LStatus = SIM800L.phoneStatus();
  switch (SIM800LStatus) {
    case 0:
      // ready
      break;
    case 2:
      // unknown
      break;
    case 3:
      // incoming call
      //SIM800L.initCall();
      SIM800L.sendATCommand("ATA");
      break;
    case 4:
      // call in progress
      break;
    case 99:
      // unknown
      break;
  }
}

void printSystemState() {
  Serial.print("GSM: ");
  switch (SIM800LStatus) {
    case 0:
      // ready
      Serial.print(F("READY   | "));
      break;
    case 2:
      // unknown
      Serial.print(F("UNKNOWN | "));
      break;
    case 3:
      // incoming call
      Serial.print(F("RING    | "));
      break;
    case 4:
      // call in progress
      Serial.print(F("IN CALL | "));
      break;
    case 99:
      // unknown
      Serial.print(F("UNKNOWN | "));
      break;
  }
  Serial.print(dayShortStr(weekday()));
  Serial.print(' ');
  Serial.print(year(), DEC);
  Serial.print('/');
  print2Digits(month());
  Serial.print('/');
  print2Digits(day());
  Serial.print(' ');
  print2Digits(hour());
  Serial.print(':');
  print2Digits(minute());
  Serial.print(':');
  print2Digits(second());
  Serial.print(" | ");
  Serial.print(systemTemperature);
  Serial.print(F("degC | Door State: "));
  if (isDoorOPEN) {
    Serial.print(F("OPENED"));
  }
  else {
    Serial.print(F("CLOSED"));
  }
  Serial.print(F(" | SMS Count: "));
  Serial.print(systemSentSMScounter);
  Serial.print(F(", Last epoch in sent SMS: "));
  Serial.print(systemLastTimestamp);
  Serial.print(F(" | Seconds since last sanity check: "));
  Serial.println(timesincelastSanitycheck);
}

void intro() {
  Serial.print(F("SIM800L Intruder Alarm ... Compiled on "));
  Serial.println(__DATE__);
  Serial.print(F("Module IMEI... "));
  Serial.println(SIM800L.moduleIMEI());
  Serial.print(F("Set Phone Function... "));
  Serial.println(SIM800L.setPhoneFunc(1));
  Serial.print(F("is Module Registered to Network?... "));
  Serial.println(SIM800L.isRegistered());
  Serial.print(F("Signal Quality... "));
  Serial.println(SIM800L.signalQuality());
  Serial.print(F("Operator Name from SIM... "));
  Serial.println(SIM800L.operatorNameFromSim());
  Serial.print(F("Operator Name... "));
  Serial.println(SIM800L.operatorName());
  Serial.print(F("Init SMS... "));
  Serial.println(SIM800L.initSMS()); // Its optional but highly recommended. Some function work with this function.
}

void sendtoSIM800L_ondoorOPEN() {
  systemLastTimestamp = systemTimestamp;
  EEPROM.updateLong(systemLastTimestampaddress, systemLastTimestamp);
  char SMStext [160];;
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Alert! Store door opened at %02d:%02d:%02d on %s %02d/%02d/%d, Temperature: %02d degC.\nTotal sent SMS count: %d.", hour(), minute(), second(), dayShortStr(weekday()), day(), month(), year(), systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo1, SMStext); // only use ascii chars please
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Alert! Store door opened at %02d:%02d:%02d on %s %02d/%02d/%d, Temperature: %02d degC.\nTotal sent SMS count: %d.", hour(), minute(), second(), dayShortStr(weekday()), day(), month(), year(), systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo2, SMStext); // only use ascii chars please
}

void sendtoSIM800L_ondoorCLOSE() {
  systemLastTimestamp = systemTimestamp;
  EEPROM.updateLong(systemLastTimestampaddress, systemLastTimestamp);
  char SMStext [160];
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Alert! Store door closed at %02d:%02d:%02d on %s %02d/%02d/%d, Temperature: %02d degC.\nTotal sent SMS count: %d.", hour(), minute(), second(), dayShortStr(weekday()), day(), month(), year(), systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo1, SMStext); // only use ascii chars please
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Alert! Store door closed at %02d:%02d:%02d on %s %02d/%02d/%d, Temperature: %02d degC.\nTotal sent SMS count: %d.", hour(), minute(), second(), dayShortStr(weekday()), day(), month(), year(), systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo2, SMStext); // only use ascii chars please
}

void sendtoSIM800L_onintervalwhileOPEN() {
  systemLastTimestamp = systemTimestamp;
  EEPROM.updateLong(systemLastTimestampaddress, systemLastTimestamp);
  char SMStext [160];
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Store door is still opened for the last %d seconds. Last epoch: %ld, Temperature: %02d degC.\nTotal sent SMS count: %d.", int(checkDoorInterval), systemLastTimestamp, systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo1, SMStext); // only use ascii chars please
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Store door is still opened for the last %d seconds. Last epoch: %ld, Temperature: %02d degC.\nTotal sent SMS count: %d.", int(checkDoorInterval), systemLastTimestamp, systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo2, SMStext); // only use ascii chars please
}

void sendtoSIM800L_onsanitycheck() {
  systemLastsanitycheckTimestamp = systemTimestamp;
  systemLastTimestamp = systemTimestamp;
  sanitycheckrequired = false;
  EEPROM.updateLong(systemLastTimestampaddress, systemLastTimestamp);
  EEPROM.updateLong(systemLastsanitycheckTimestampaddress, systemLastsanitycheckTimestamp);
  EEPROM.updateInt(sanitycheckrequiredaddress, sanitycheckrequired);
  systemInboxSMScount = SIM800L.list(false);
  char SMStext [160];
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Store door alarm system sanity check! Temperature: %02d degC.\nTotal sent SMS count: %d.\nInbox is being purged now.", systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo1, SMStext); // only use ascii chars please
  systemSentSMScounter++;
  EEPROM.updateInt(systemSentSMScounteraddress, systemSentSMScounter);
  sprintf (SMStext, "Store door alarm system sanity check! Temperature: %02d degC.\nTotal sent SMS count: %d.\nInbox is being purged now.", systemTemperature, systemSentSMScounter);
  SIM800L.send(PhoneNo2, SMStext); // only use ascii chars please
  SIM800L.deleteAll();
}

void clearEEPROMvalues() {
  EEPROM.updateInt(systemSentSMScounteraddress, 0);
  EEPROM.updateLong(systemLastTimestampaddress, 0);
  EEPROM.updateLong(systemLastsanitycheckTimestampaddress, systemTimestamp);
}

void print2Digits(int digits) {
  if (digits < 10) {
    Serial.print('0');
  }
  Serial.print(digits);
}
