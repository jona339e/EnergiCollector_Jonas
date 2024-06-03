#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include "time.h"
#include <SD.h>
#include <ArduinoJson.h>

// for interrupt
const int interruptPin = 13; // change if connected to another pin 


// for config
struct Config {
  char ssid[32];
  char password[32];
  char ip[32];
  char gateway[32];
};
Config config;


// for wifi
WiFiClient client;
IPAddress subnet(255, 255, 255, 0);


// for asyncWebserver
AsyncWebServer server(80);


//for websocket
AsyncWebSocket ws("/ws");


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


// for websocket & server
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


// shared
xQueueHandle logQueue;
SemaphoreHandle_t SDMutex;


// prototypes
void IRAM_ATTR isrImpulse();
void setupWifi();
void setupSD();
int setupConfig();
void saveConfig();
void createAccessPoint();
void hostConfigHTML();
void mdnsInit();
void websocketInit();
void addRoutes();
void notifyClient();
void handleWebSocketEvent();
String processor(const String& var);



void setup() {
  Serial.begin(115200);
  // set pin to low
  pinMode(interruptPin, INPUT_PULLDOWN);
  digitalWrite(interruptPin, LOW);


  // setup sd card
  setupSD();


  // setup config file
  switch (setupConfig())
  {
    case 0:
      // error
      Serial.println("An error has occured while setting up config file");
      break;
    case 1:
      // config file is empty
      createAccessPoint();
      hostConfigHTML();

      break;
    case 2:
      // config file is not empty
      
      break;

  }
  

  // setup wifi
  setupWifi();


  // setup time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);


  

  // create queue
  logQueue = xQueueCreate(20, sizeof(dataLog));


  // create mutex


  // setup tasks



  // after setup, attatch interrupt
  attachInterrupt(digitalPinToInterrupt(interruptPin), isrImpulse, RISING);

}


void loop() {

}


// wifi connection
void setupWifi(){
  WiFi.begin(config.ssid, config.password);
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


// sd card setup reference - https://randomnerdtutorials.com/esp32-microsd-card-arduino/
void setupSD(){
  if(!SD.begin(5)){
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

}

/* SUMMARY:
  returns 0 if an error occurs
  return 1 if config file is empty
  return 2 if config file is not empty
*/
int setupConfig(){
  // initialize LittleFS
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    return 0;
  }

  // read config file
  File configFile = LittleFS.open("/config.json", "r");
  if(!configFile){
    Serial.println("Failed to open config file");
    return 0;
  }

  // read config file and set ssid and password
  // from the arduinoJson.h library examples: https://arduinojson.org/v6/example/config/
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if(error){
    Serial.println("Failed to read file, using default configuration");
    return 0;
  }

  // give config struct the values from the config file
  strlcpy(config.ssid, doc["ssid"] | "", sizeof(config.ssid));
  strlcpy(config.password, doc["password"] | "", sizeof(config.password));
  strlcpy(config.ip, doc["ip"] | "", sizeof(config.ip));
  strlcpy(config.gateway, doc["gateway"] | "", sizeof(config.gateway));

  configFile.close();
  
  // check if config is empty
  // since char arrays are null terminated, we can check if the first element is null to figure out if it is empty (i hope)
  if (config.password[0] == '\0' || config.ssid[0] == '\0' || config.ip[0] == '\0' || config.gateway[0] == '\0')
  {
    return 1;
  }

  return 2;

}


// save config to file - not implemented yet
void saveConfig(){

}


// create access point - not implemented yet
void createAccessPoint(){

}


// host configHTML - not implemented yet
void hostConfigHTML(){

}


// mdns init - not implemented yet
void mdnsInit(){

}


// websocket init - not implemented yet
void websocketInit(){

}


// add routes - not implemented yet
void addRoutes(){

}


// notify client - not implemented yet
void notifyClient(){

}


// handle websocket event - not implemented yet
void handleWebSocketEvent(){

}


// route request processor - not implemented yet
String processor(const String& var){

  return String();
}




#pragma region Opgave formulering
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


/*
  video aflevering

  - fortæl hvad projektet går ud på

  - vis hvordan det virker

  - afrund projektet, har jeg lært noget og hvordan kan jeg komme videre.
*/
#pragma endregion