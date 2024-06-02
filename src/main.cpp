#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include "time.h"


// for interrupt
const int interruptPin = 13; // change if connected to another pin 


// for wifi
const char* ssid = "";
const char* password = "";


// for time -- reference: https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/
const char* ntpServer = "0.dk.pool.ntp.org"; // https://www.ntppool.org/zone/dk taget herfra
const long gmtOffset_sec = 3600;  
const int daylightOffset_sec = 3600;
struct tm timeinfo;


// for logging
struct dataLog {
  int accumulatedValue;
  time_t time;
};
volatile int accumulatedValue = 0;


// shared
xQueueHandle logQueue;
SemaphoreHandle_t SDMutex;


// prototypes
void IRAM_ATTR isrImpulse();
void setupWifi();


void setup() {
  Serial.begin(115200);
  // set pin to low
  pinMode(interruptPin, INPUT_PULLDOWN);
  digitalWrite(interruptPin, LOW);
  // setup sd card


  // setup config file


  // if config.file is empty
  // setup configuration html page
  // then reboot




  // setup wifi
  setupWifi();


  // setup time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  // create queue
  logQueue = xQueueCreate(20, sizeof(dataLog));


  // after setup, attatch interrupt
  attachInterrupt(digitalPinToInterrupt(interruptPin), isrImpulse, RISING);

}


void loop() {

}


// setup functions

// wifi connection
void setupWifi(){
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
}




// interrupt function
void IRAM_ATTR isrImpulse(){
  // on interrupt rising edge from the 2nd esp32
  // send queue message to be handled in a task
  // message should contain the time of the interrupt & accumulated impulses

  // create log
  dataLog log;

  // get time
  getLocalTime(&timeinfo);

  // set time  and accumulatedvalue in log
  log.time = mktime(&timeinfo);
  log.accumulatedValue = ++accumulatedValue;

  // send log to queue
  xQueueSendFromISR(logQueue, &log, NULL);
  
  // set pinmode to low
  digitalWrite(interruptPin, LOW);
}





/*
  Program projekt:

  Få et input / signal fra en sensor eller andet, og opsaml det på esp32'eren.
  Derefter præsenter det på en hjemmeside som esp32'eren hoster.

  Esp32'eren skal også selv kunne konfigurere nætværket, så den kan forbinde til en router via SSID og password.
  Denne konfiugration skal gemmes i en fil, så den kan huske det næste gang den tændes i data mappen via spiffs eller littleFS.

  Den indsamlede data skal også gemmes i en csv fil. dette skal gøres på et SD-Kort.
  på SD-Kortet skal der også ligges filer som bruges til at vise hjemmesiderne på.


  Hjemmeside funkitonalitet:
  - Vise data fra sensoren (csv fil)
  - slette data fra sensoren (csv fil)
  - Sæt initial value for data sensor (csv fil)
  - download csv fil med data
  - opsætte wifi
  - reset esp32 konfiguration (wifi)


  esp 1 funktionalitet:
  - send impulser i et interval på 10 sekunder.
  - afgør hvor mange impulser der skal sendes via math.rand.
  - send impuls hvert 80ms.
  
  - forbind aldrig et output til et output
  - forbind gnd til gnd for at få en fælles reference

  esp 2 funktionalitet:
  - host hjemmeside via async 
  - benyt websocket til live update
  - håndter konfiguartion af wifi
  - modtag impulser fra esp 1
  - gem impulser i en csv fil på SD-kort


*/