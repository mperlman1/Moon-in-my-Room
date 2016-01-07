/*
 * Moon_in_my_Room sketch
 * 
 * Used http://www.imagitronics.org/projects/rc-moon/ and 
 * http://www.instructables.com/id/Arduino-Controlled-Model-Moon-synchronizes-phase-c/
 * as references
 * 
 * Get schematic from: https://github.com/mperlman1/Moon-in-my-Room
 * 
 * Requires libraries:
 * RTC_DS3231 (https://github.com/mizraith/RTClib)
 * Tlc5940 (https://code.google.com/p/tlc5940arduino/)
 * 
 * Assumes that RTC is already set to current date/time
 * The exact setting is mostly irrelevant since this just uses the year/month/day to
 * calculate the phase
 * 
 * Basic logic:
 * Turn on when the room is dark
 * Get moon phase based on current date
 * Activate moon segments and scroll through colors
 * Turn off after a period of time or when the room is light
 * 
 */

#include <avr/sleep.h>
#include <Wire.h>
#include "Tlc5940.h"
#include <SPI.h>
#include "RTC_DS3231.h"

// uncomment the following line to turn on serial logging
// NOTE: this will likely break the color cycling
//#define DEBUG

RTC_DS3231 RTC;

int led1GreenPin = 5;                // green cathode for LED 1
int led1BluePin = 6;                 // blue cathode for LED 1
int ldrPin = 2;                      // pin for the LDR to sense light in the room

volatile boolean initialized = false;// boolean to indicate that we have just awakened from sleep

long turnOnTime, turnOffTime;        // time (in seconds past the epoch) that the moon turned on and
                                     // when it's planned to turn off to conserve battery

long currentMillis, previousMillis;  // current time (in millis) and time of last time check
long nextColorCycle;                 // time (in millis) when the next color cycle will begin
const int stepDuration = 100;        // duration (in millis) of each cycle step
const int minCycleDuration = 3000;   // minimum color cycle duration (in milliseconds)
const int maxCycleDuration = 8000;   // maximum color cycle duration (in milliseconds)
int cycleSteps, currentCycleStep;    // total cycle steps and current step number
int redTarget[6], greenTarget[6], blueTarget[6];
                                     // target colors for the current color cycle
int currentRed[6], currentGreen[6], currentBlue[6];
                                     // current RGB value
int redStep[6], greenStep[6], blueStep[6];
                                     // step size for each color in current cycle

boolean activeSegments[6];           // array that identifies which segements should be one for this phase

const int minOnTime = 720;           // minimum time to run (in seconds) when the room is dark
const int maxOnTime = 1080;          // maximum time to run (in seconds) when the room is dark
// NOTE: standard Moon in my Room run time appears to be ~30 min


void setup() {
  #ifdef DEBUG
  Serial.begin(9600);                // open serial communications to PC for updates
  #endif
  
  Wire.begin();                      // begin communication with RTC module
  RTC.begin();
  Tlc.init();

  pinMode(ldrPin, INPUT);                                
  pinMode(led1GreenPin, OUTPUT);
  pinMode(led1BluePin, OUTPUT);
  analogWrite(led1GreenPin, 255);
  analogWrite(led1BluePin, 255);

  randomSeed(analogRead(0));
}

void loop() {
  if((digitalRead(ldrPin)) || moonIsReadyToSleep()) {          // if ldrPin is high, the room is too bright to operate
    #ifdef DEBUG
    Serial.println("going to sleep");
    #endif
    turnMoonOff();
    delay(10);
    sleepNow();                      // so we sleep to arduino to conserve battery
  }

  if(!initialized) {
    /*
     * This is more or less a setup() function inside the main loop to perform initial
     * housekeeping when the moon wakes up.
     * 
     * Basic logic
     * Turn everything off
     * Read the current date
     * Identify the moon phase and determine which segments are active
     * Determine how long to run
     * Initialize the color cycle
     */
    initialized = true;
    turnMoonOff();
    DateTime now = RTC.now();
    
    #ifdef DEBUG
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
    #endif

    setPhase(now.year(), now.month(), now.day());

    turnOnTime = now.unixtime();
    turnOffTime = turnOnTime + random(minOnTime, maxOnTime + 1);

    #ifdef DEBUG
    Serial.print("Turned on: ");
    Serial.println(turnOnTime);
    Serial.print("Plan to turn off: ");
    Serial.println(turnOffTime);
    #endif

    for (int segment = 0; segment < 6; segment++) {
      switch (segment) {
        case 0:
          currentRed[segment] = random(0, 4096);
          currentGreen[segment] = 255 - random(0, 256);
          currentBlue[segment] = 255 - random(0, 256);
          break;
        default:
          currentRed[segment] = random(0, 4096);
          currentGreen[segment] = random(0, 4096);
          currentBlue[segment] = random(0, 4096);
          break;
      }
    }
    establishNextColorCycle();
  }

  if(initialized) {
    updateMoon();
  }
  
} 

void wakeNow() {
  /*
   * Interrupt handler. Only action is to set value of initialized to false.
   */
  initialized = false;
}


void sleepNow() {
  /*
   * Put the moon to sleep. Only wake up when the room goes from light to dark.
   */
    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    attachInterrupt(digitalPinToInterrupt(ldrPin), wakeNow, FALLING);
    sleep_mode();
    
    // THE PROGRAM CONTINUES FROM HERE AFTER WAKING UP
    sleep_disable();
    detachInterrupt(digitalPinToInterrupt(2));
    delay(10);
}


void setPhase(int Y, int M, int D) {
  /*
   * caculate the current phase of the moon and set the active segments
   * 
   * based on the current date algorithm adapted from Stephen R. Schmitt's
   * Lunar Phase Computation program, originally written in the Zeno
   * programming language (http://home.att.net/~srschmitt/lunarphasecalc.html)
   * 
   */
  double AG, IP;
  byte phase;

  long YY, MM, K1, K2, K3, JD;
                              
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

  IP = normalize((JD - 2451550.1) / 29.530588853);
  AG = IP*29.53;
  
  if(AG < 1.20369)
    setActiveSegments(false, false, false, false, false, false);
  else if(AG < 3.61108)
    setActiveSegments(true, false, false, false, false, false);
  else if(AG < 6.01846)
    setActiveSegments(true, true, false, false, false, false);
  else if(AG < 8.42595)
    setActiveSegments(true, true, true, false, false, false);
  else if(AG < 10.83323)
    setActiveSegments(true, true, true, true, false, false);
  else if(AG < 13.24062)
    setActiveSegments(true, true, true, true, true, false);
  else if(AG < 15.64800)
    setActiveSegments(true, true, true, true, true, true);
  else if(AG < 18.05539)
    setActiveSegments(false, true, true, true, true, true);
  else if(AG < 20.46277)
    setActiveSegments(false, false, true, true, true, true);
  else if(AG < 22.87016)
    setActiveSegments(false, false, false, true, true, true);
  else if(AG < 25.27754)
    setActiveSegments(false, false, false, false, true, true);
  else if(AG < 27.68493)
    setActiveSegments(false, false, false, false, false, true);
  else
    setActiveSegments(false, false, false, false, false, false);
}

double normalize(double v) {           // normalize moon calculation between 0-1
    v = v - floor(v);
    if (v < 0)
        v = v + 1;
    return v;
}

void updateMoon() {
  currentMillis = millis();

  #ifdef DEBUG
  Serial.print("Current: ");
  Serial.println(currentMillis);
  Serial.print("Previous: ");
  Serial.println(previousMillis);
  #endif
  
  if((currentMillis - previousMillis) < stepDuration) return;

  previousMillis = currentMillis;

  #ifdef DEBUG
  Serial.print("Current Millis: ");
  Serial.println(currentMillis);
  Serial.print("Next Cycle Start: ");
  Serial.println(nextColorCycle);
  Serial.print("Current Cycle Step: ");
  Serial.println(currentCycleStep);
  Serial.print("Cycle Steps: ");
  Serial.println(cycleSteps);
  #endif

  if ((currentCycleStep == cycleSteps) || (currentMillis >= nextColorCycle)) 
    establishNextColorCycle();

  for (int segment = 0; segment < 6; segment++) {
    currentRed[segment] += redStep[segment];
    currentGreen[segment] += greenStep[segment];
    currentBlue[segment] += blueStep[segment];
  }
  currentCycleStep++;

  #ifdef DEBUG
  Serial.print("Cycle Step: ");
  Serial.println(currentCycleStep);
  #endif

  for (int segment = 5; segment >=0; segment--) {
    #ifdef DEBUG
    Serial.print("Segment: ");
    Serial.println(segment);
    #endif
    if (activeSegments[segment] == false) continue;
    switch (segment) {
    case 0:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(0, currentRed[segment]);
      analogWrite(led1GreenPin, currentGreen[segment]);
      analogWrite(led1BluePin, currentBlue[segment]);
      break;
    case 1:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(1, currentRed[segment]);
      Tlc.set(2, currentGreen[segment]);
      Tlc.set(3, currentBlue[segment]);
      break;
    case 2:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(4, currentRed[segment]);
      Tlc.set(5, currentGreen[segment]);
      Tlc.set(6, currentBlue[segment]);
      break;
    case 3:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(7, currentRed[segment]);
      Tlc.set(8, currentGreen[segment]);
      Tlc.set(9, currentBlue[segment]);
      break;
    case 4:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(10, currentRed[segment]);
      Tlc.set(11, currentGreen[segment]);
      Tlc.set(12, currentBlue[segment]);
      break;
    case 5:
      #ifdef DEBUG
      Serial.print("Adjusting segment: ");
      Serial.println(segment);
      #endif
      Tlc.set(13, currentRed[segment]);
      Tlc.set(14, currentGreen[segment]);
      Tlc.set(15, currentBlue[segment]);
      break;
    }
  }
  Tlc.update();
}

void turnMoonOff() {
  Tlc.clear();
  Tlc.update();
  digitalWrite(led1GreenPin, HIGH);
  digitalWrite(led1BluePin, HIGH);
}

boolean moonIsReadyToSleep() {
  /*
   * Determine if the moon is ready to sleep
   * 
   * Don't evaluate if the moon isn't initialized
   * 
   * Possible turn off conditions:
   * Moon has been on long enough
   * New moon (nothing is lit up so conserve energy and sleep)
   * 
   */
  if(!initialized) return false;
  DateTime now = RTC.now();
  if(turnOffTime < now.unixtime()) return true;
  if((activeSegments[0] == false) && (activeSegments[1] == false) &&
     (activeSegments[2] == false) && (activeSegments[3] == false) &&
     (activeSegments[4] == false) && (activeSegments[5] == false)) return true;
  return false;
}

void setActiveSegments(boolean first, boolean second, boolean third, boolean fourth, boolean fifth, boolean sixth) {
  activeSegments[0] = first;
  activeSegments[1] = second;
  activeSegments[2] = third;
  activeSegments[3] = fourth;
  activeSegments[4] = fifth;
  activeSegments[5] = sixth;
}

void establishNextColorCycle() {
  currentMillis = millis();
  previousMillis = currentMillis;
  nextColorCycle = currentMillis + random(minCycleDuration, maxCycleDuration + 1);
  currentCycleStep = 0;
  cycleSteps = (nextColorCycle - currentMillis) / stepDuration;
  
  for (int segment = 0; segment < 6; segment++) {
    switch(segment) {
      case 0:
        redTarget[segment] = random(0, 4096);
        greenTarget[segment] = 255 - random(0, 256);
        blueTarget[segment] = 255 - random(0, 256);
        break;
      default:
        redTarget[segment] = random(0, 4096);
        greenTarget[segment] = random(0, 4096);
        blueTarget[segment] = random(0, 4096);
        break;
    }
    redStep[segment] = (redTarget[segment] - currentRed[segment]) / cycleSteps;
    greenStep[segment] = (greenTarget[segment] - currentGreen[segment]) / cycleSteps;
    blueStep[segment] = (blueTarget[segment] - currentBlue[segment]) / cycleSteps;
  }

  #ifdef DEBUG
  Serial.print("Current millis: ");
  Serial.println(currentMillis);
  Serial.print("Next cycle start: ");
  Serial.println(nextColorCycle);
  Serial.print("Steps in cycle: ");
  Serial.println(cycleSteps);
  #endif
}
