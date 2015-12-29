#include <avr/sleep.h>
#include <Wire.h>
#include "Tlc5940.h"
#include <SPI.h>
#include "RTC_DS3231.h"

RTC_DS3231 RTC;
int led1GreenPin = 5;
int led1BluePin = 6;
int ldrPin = 2;                      // pin for the LDR to sense light in the room
volatile boolean started = true;     // boolean to indicate that we have just awakened from sleep
int fullYear;                        // full year (ie 2009) to be retrieved from EEPROM
byte second, minute, hour, dayOfWeek, dayOfMonth, month, year;
                                     // date and time retrieved from DS3231 RTC module
long turnOffTime;                    // time (in seconds past the epoch) to turn off to conserve battery

void setup() {                       // arduino setup routine
  Serial.begin(9600);                // open serial communications to PC for updates
  Wire.begin();                      // begin communication with DS1307 RTC module
  RTC.begin();
  Tlc.init();

  pinMode(ldrPin, INPUT);            // configure the LDR pin as input                                 
  pinMode(led1GreenPin, OUTPUT);
  pinMode(led1BluePin, OUTPUT);
  analogWrite(led1GreenPin, 255);
  analogWrite(led1BluePin, 255);
}

void loop() {
  //Serial.println("IN THE LOOP");
  DateTime now = RTC.now();
  
  if(digitalRead(ldrPin)) {          // if ldrPin is high, the room is too bright to operate
    Serial.println("going to sleep");
    updateMoon(0);
    delay(10);
    sleepNow();                      // so we sleep to arduino to conserve battery
  }
  
  if(started) {                      // if the device has just awakened from sleep
    started = false;                 // it has no longer 'just' awakened, so this
                                     // code should only be run once
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(' ');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();
    
    second = now.second();
    minute = now.minute();
    hour = now.hour();
    dayOfMonth = now.day();
    month = now.month();
    year = now.year()-2000;
    fullYear = now.year();  // get the full four year date

    turnOffTime = now.unixtime() + 15 * 60; // turn off in 15 minutes
                                     
    Serial.print("second ");
    Serial.println(second, DEC);
    Serial.print("minute ");
    Serial.println(minute, DEC);
    Serial.print("hour ");
    Serial.println(hour, DEC);
    Serial.print("dayofweek ");
    Serial.println(dayOfWeek, DEC);
    Serial.print("dayOfMonth ");
    Serial.println(dayOfMonth, DEC);
    Serial.print("month ");
    Serial.println(month, DEC);
    Serial.print("fullyear ");
    Serial.println(fullYear, DEC);
    
    updateMoon(getPhase(fullYear, int(month), int(dayOfMonth)));

  }
} 

void wakeNow() {                    // here the interrupt is handled after wakeup
  started = true;                   // variable establishes that we have just awakened
}


void sleepNow() {                    // put the arduino to sleep
    started = false;
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    //set_sleep_mode(SLEEP_MODE_IDLE);
    sleep_enable();                  // enables the sleep bit in the mcucr register
                                     // so sleep is possible. just a safety pin 
    attachInterrupt(digitalPinToInterrupt(2), wakeNow, LOW);
                                     // use interrupt on pin 2 and run function wakeNow
    sleep_mode();                    // put the arduino to sleep!
    
    // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP
    sleep_disable();                 // first thing after waking from sleep
    detachInterrupt(digitalPinToInterrupt(2));
                                     // disables interrupt on pin 2 so the 
                                     // wakeUpNow code will not be executed 
    delay(10);                      // during normal operation and wait a bit
}


byte getPhase(int Y, int M, int D) {  // calculate the current phase of the moon
  double AG, IP;                      // based on the current date
  byte phase;                         // algorithm adapted from Stephen R. Schmitt's
                                      // Lunar Phase Computation program, originally
  long YY, MM, K1, K2, K3, JD;        // written in the Zeno programming language
                                      // http://home.att.net/~srschmitt/lunarphasecalc.html
  // calculate julian date
  YY = Y - floor((12 - M) / 10);
  MM = M + 9;
  if(MM >= 12)
    MM = MM - 12;
  
  K1 = floor(365.25 * (YY + 4712));
  K2 = floor(30.6 * MM + 0.5);
  K3 = floor(floor((YY / 100) + 49) * 0.75) - 38;

  JD = K1 + K2 + D + 59;
  if(JD > 2299160)
    JD = JD -K3;

  Serial.print("Julian date: ");
  Serial.println(JD);
  
  IP = normalize((JD - 2451550.1) / 29.530588853);
  AG = IP*29.53;

  Serial.print("IP: ");
  Serial.println(IP);
  Serial.print("AG: ");
  Serial.println(AG);
  
  if(AG < 1.20369)
    phase = B00000000;
  else if(AG < 3.61108)
    phase = B00000001;
  else if(AG < 6.01846)
    phase = B00000011;
  else if(AG < 8.42595)
    phase = B00000111;
  else if(AG < 10.83323)
    phase = B00001111;
  else if(AG < 13.24062)
    phase = B00011111;
  else if(AG < 15.64800)
    phase = B00111111;
  else if(AG < 18.05539)
    phase = B00111110;
  else if(AG < 20.46277)
    phase = B00111100;
  else if(AG < 22.87016)
    phase = B00111000;
  else if(AG < 25.27754)
    phase = B00110000;
  else if(AG < 27.68493)
    phase = B00100000;
  else
    phase = 0;

  Serial.print("Phase: ");
  Serial.println(phase);
  
  return phase;    
}

double normalize(double v) {           // normalize moon calculation between 0-1
    v = v - floor(v);
    if (v < 0)
        v = v + 1;
    return v;
}

void updateMoon(byte thePhase) {
  for(byte mask = 00000001, j=7; mask<01000000, j<13; mask<<=1, j++) {
    Serial.print("Mask: ");
    Serial.println(mask);
    Serial.print("j: ");
    Serial.println(j);
    Serial.print("Bit mask: ");
    Serial.println(thePhase & mask);
    if((thePhase & mask))
      turnOnMoonSegment(j);
    else
      turnOffMoonSegment(j);
  }
}

void turnOnMoonSegment(byte segment) {
  Serial.print("In TurnOnMoonSegment for segment: ");
  Serial.println(segment);
  switch (segment) {
    case 7:
      Tlc.set(0, 4095);
      Tlc.update();
      digitalWrite(led1GreenPin, HIGH);
      digitalWrite(led1BluePin, HIGH);
      break;
    case 8:
      Tlc.set(1, 0);
      Tlc.set(2, 626);
      Tlc.set(3, 2216);
      Tlc.update();
      break;
    case 9:
      Tlc.set(4, 0);
      Tlc.set(5, 4095);
      Tlc.set(6, 4095);
      Tlc.update();
      break;
    case 10:
      Tlc.set(7, 0);
      Tlc.set(8, 2056);
      Tlc.set(9, 0);
      Tlc.update();
      break;
    case 11:
      Tlc.set(10, 4095);
      Tlc.set(11, 0);
      Tlc.set(12, 0);
      Tlc.update();
      break;
    case 12:
      Tlc.set(13, 2056);
      Tlc.set(14, 0);
      Tlc.set(15, 2056);
      Tlc.update();
      break;
  }
}

void turnOffMoonSegment(byte segment) {
  Serial.print("In TurnOffMoonSegment for segment: ");
  Serial.println(segment);
  switch (segment) {
    case 7:
      Tlc.set(0, 0);
      Tlc.update();
      digitalWrite(led1GreenPin, HIGH);
      digitalWrite(led1BluePin, HIGH);
      break;
    case 8:
      Tlc.set(1, 0);
      Tlc.set(2, 0);
      Tlc.set(3, 0);
      Tlc.update();
      break;
    case 9:
      Tlc.set(4, 0);
      Tlc.set(5, 0);
      Tlc.set(6, 0);
      Tlc.update();
      break;
    case 10:
      Tlc.set(7, 0);
      Tlc.set(8, 0);
      Tlc.set(9, 0);
      Tlc.update();
      break;
    case 11:
      Tlc.set(10, 0);
      Tlc.set(11, 0);
      Tlc.set(12, 0);
      Tlc.update();
      break;
    case 12:
      Tlc.set(13, 0);
      Tlc.set(14, 0);
      Tlc.set(15, 0);
      Tlc.update();
      break;
  }
}

