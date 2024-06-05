#include <Arduino.h>
#include <SPI.h>
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
  IPAddress ip;
  IPAddress gateway;
};
Config config;


// for wifi
WiFiClient client;
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

// for webserver
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");


// for time -- reference: https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/
const char* ntpServer = "pool.ntp.org"; // https://www.ntppool.org/zone/dk taget herfra
const long gmtOffset_sec = 3600;  
const int daylightOffset_sec = 3600;
struct tm timeinfo;

// for logging
struct dataLog {
  int accumulatedValue;
  time_t time;
};
volatile int accumulatedValue = 0;
volatile dataLog latestData;



// shared
xQueueHandle logQueue;
SemaphoreHandle_t SDMutex;


// prototypes
void IRAM_ATTR isrImpulse();
bool setupWifi();
void setupSD();
int setupConfig();
void saveConfig();
void createAccessPoint();
void websocketInit();
void addRoutes();
void handleWebSocketEvent(void *arg, uint8_t *data, size_t len);
void notifyClientWholeLog();
void notifyClientSingleLog(dataLog log);
void sendLogToClient(AsyncWebSocketClient *client);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len);
void websocketCleanup( void * pvParameters );
void handleData( void * pvParameters);
void simulateImpulse( void * pvParameters);
void createDataLog();
void addDataLog(dataLog log);
void deleteDataLogFile();



void setup() {
  Serial.begin(115200);
  // set pin to low
  pinMode(interruptPin, OUTPUT);
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
      return; // make sure to return or the setup will continue

      break;
    case 2:
      // config file is not empty so do nothing

     break;
  }


  // setup wifi
  if(!setupWifi()){
    // if failed to connect to wifi, create access point
    createAccessPoint();
    return;
  }


  // setup websocket
  websocketInit();
  addRoutes();

  vTaskDelay(1000);

  // setup time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  // struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  // print out time
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");


  // create queue & create mutex
  logQueue = xQueueCreate(100, sizeof( struct dataLog));
  SDMutex = xSemaphoreCreateMutex();


  // setup tasks
  xTaskCreate(websocketCleanup, "websocketCleanup", 2048, NULL, 1, NULL);
  xTaskCreate(handleData, "handleData", 4096, NULL, 2, NULL);
  xTaskCreate(simulateImpulse, "simulateImpulse", 2048, NULL, 3, NULL);


  // after setup, attatch interrupt
  vTaskDelay(1000);
  // attachInterrupt(digitalPinToInterrupt(interruptPin), isrImpulse, RISING);


}


void loop() {

}


// wifi connection
bool setupWifi(){
  IPAddress ip;
  // WiFi.mode(WIFI_STA);

  WiFi.config(config.ip, config.gateway, subnet, dns);
  WiFi.begin(config.ssid, config.password);
  Serial.print("Connecting to WiFi.."); 
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if(i >= 30)
    {
      Serial.println("Failed to connect to WiFi");
      return false;
    }
    vTaskDelay(1000);
    Serial.print(".");
  }
  Serial.println();
  if(!MDNS.begin("Energy_Collector")){
    Serial.println("Error setting up MDNS responder");
    return false;
  }
  // MDNS.addService("http", "tcp", 80);
  Serial.println("Connected to WiFi");
  Serial.println("Address: Energy_Collector.local");
  // Serial.println("IpAddress: " + WiFi.localIP());

  return true;
}


// sd card setup reference - https://randomnerdtutorials.com/esp32-microsd-card-arduino/
void setupSD(){
  if(!SD.begin()){
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();

  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  // check if json file exist
  // if it doesn't exist, create it
  if(!SD.exists("/dataLog.json")){
    Serial.println("Creating dataLog.json");
    createDataLog();
  }
  else{
    Serial.println("Reading dataLog.json");

    // Open the dataLog.json file
    File dataLogFile = SD.open("/dataLog.json", FILE_READ);
    if(!dataLogFile){
      Serial.println("Failed to open dataLog file");
      return;
    }
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, dataLogFile);
    if(error){
      Serial.println("Failed to read file, using default configuration");
      return;
    }
    dataLogFile.close();

    // Parse the JSON content
    JsonArray logArray = doc["log"].as<JsonArray>();

    // Print the size of the JSON array
    Serial.print("Log array size: ");
    Serial.println(logArray.size());

    // Ensure the array is not empty
    if (logArray.size() > 0) {
      // Get the last element
      JsonObject lastLog = logArray[logArray.size() - 1];

      // Extract the last accumulated value
      accumulatedValue = lastLog["accumulatedValue"].as<int>();

      Serial.print("Last Accumulated Value: ");
      Serial.println(accumulatedValue);
      vTaskDelay(2000);
    } else {
      Serial.println("No log entries found");
    }
  }
}


/// @brief Returns 0 if an error has occurred while mounting LittleFS, 1 if the config file is empty, and 2 if the config file is not empty.
/// @return int 0, 1 or 2
/// @note This function reads the config file and sets the ssid, password, ip and gateway from the config file.
/// @param void
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
  String localSSID = doc["ssid"];
  String localPassword = doc["password"];
  String localIP = doc["ip"];
  String localGateway = doc["gateway"];

  // copy values to config struct
  strlcpy(config.ssid, localSSID.c_str(), sizeof(config.ssid));
  strlcpy(config.password, localPassword.c_str(), sizeof(config.password));
  config.ip.fromString(localIP);
  config.gateway.fromString(localGateway);


  // write out the config
  Serial.println("Config file content:");
  Serial.println(config.ssid);
  Serial.println(config.password);
  Serial.println(config.ip);
  Serial.println(config.gateway);

  configFile.close();
  
  // check if config is empty
  // since char arrays are null terminated, we can check if the first element is null to figure out if it is empty (i hope)
  if (config.password[0] == '\0' || config.ssid[0] == '\0' || config.ip[0] == '\0' || config.gateway[0] == '\0')
  {
    Serial.println("Config file is empty");
    return 1;
  }

  return 2;

}


// save config to file
void saveConfig(){
  Serial.println("Saving config to file");
  // save config to file
  File configFile = LittleFS.open("/config.json", "w");
  if(!configFile){
    Serial.println("Failed to open config file for writing");
    return;
  }

  // create json object
  JsonDocument doc;
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;
  doc["ip"] = config.ip;
  doc["gateway"] = config.gateway;

  // serialize json object to file
  if(serializeJson(doc, configFile) == 0){
    Serial.println("Failed to write to file");
  }

  configFile.close();

}


// create access point
void createAccessPoint(){
  Serial.println("Setting AP (Energy_Collector_Wifi)");
  WiFi.softAP("Energy_Collector_Wifi", NULL);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  // route for serving html
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/wifimanager.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // HTTP POST ssid value
        if (p->name() == "ssid") {
          strlcpy(config.ssid, p->value().c_str(), sizeof(config.ssid));
          Serial.print("SSID set to: ");
          Serial.println(config.ssid);

        }
        // HTTP POST pass value
        if (p->name() == "pass") {
          strlcpy(config.password, p->value().c_str(), sizeof(config.password));
          Serial.print("Password set to: ");
          Serial.println(config.password);

        }
        // HTTP POST ip value
        if (p->name() == "ip") {
          config.ip.fromString(p->value().c_str());
          Serial.print("IP Address set to: ");
          Serial.println(config.ip);

        }
        // HTTP POST gateway value
        if (p->name() == "gateway") {
          config.gateway.fromString(p->value().c_str());
          Serial.print("Gateway set to: ");
          Serial.println(config.gateway);

        }
        
      }
    }
    request->send(200, "text/plain", "Done. ESP will restart, connect to your router and go to IP address: " + config.ip.toString());
    saveConfig();
    vTaskDelay(3000);
    ESP.restart();
  });
  server.begin();
}


// websocket init - not implemented yet
void websocketInit(){
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}


void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len){

// WS_EVT_CONNECT when a client has logged in,
// WS_EVT_DISCONNECT when a client has logged out,
// WS_EVT_DATA when a data packet is received from the client.
// WS_EVT_PONG in response to a ping request,
// WS_EVT_ERROR when an error is received from the client,

  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      // Send the entire log to the newly connected client
      sendLogToClient(client);
      break;
    case WS_EVT_DISCONNECT:
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      // Handle data
      handleWebSocketEvent(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}



void sendLogToClient(AsyncWebSocketClient *client) {
  File dataLogFile = SD.open("/dataLog.json", "r");
  if (!dataLogFile) {
    Serial.println("Failed to open dataLog file");
    return;
  }

  // Create JSON object
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, dataLogFile);
  dataLogFile.close();

  if (error) {
    Serial.println("Failed to read file, using default configuration");
    return;
  }

  // Serialize JSON object to string
  String output;
  serializeJson(doc, output);

  // Send JSON object to the connected client
  client->text(output);
}



// add routes - not implemented yet
void addRoutes(){


  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", String(), false);
  });
  

  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });


  // Route to load script.js file
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "text/javascript");
  });


  server.begin();

}


// handle websocket event - not implemented yet
void handleWebSocketEvent(void *arg, uint8_t *data, size_t len){
// handle websocket event when data is recieved from client.
// this could be the client requesting to download the whole log.
// or the client requesting to download a single log.

  // create json object
  JsonDocument doc;
  deserializeJson(doc, data, len);

  // check if the client wants the whole log
  if(doc["request"] == "wholeLog"){
    notifyClientWholeLog();
  }

  // check if the client wants a single log
  if(doc["request"] == "singleLog"){
    dataLog log;
    log.accumulatedValue = doc["accumulatedValue"];
    log.time = doc["time"];
    notifyClientSingleLog(log);
  }

  String request = doc["request"];
  if (request == "deleteDataLogFile") {
    // Call the function to delete the data log file
    deleteDataLogFile();
  }



}


// send json data to client
void notifyClientWholeLog(){
  File dataLogFile = SD.open("/dataLog.json", "r");
  if (!dataLogFile) {
    Serial.println("Failed to open dataLog file");
    return;
  }

  // Create JSON object
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, dataLogFile);
  dataLogFile.close();

  if (error) {
    Serial.println("Failed to read file, using default configuration");
    return;
  }

  // Serialize JSON object to string
  String output;
  serializeJson(doc, output);

  // Send JSON object to all connected clients
  ws.textAll(output);
}


void notifyClientSingleLog(dataLog log){
  JsonDocument doc;
  doc["accumulatedValue"] = log.accumulatedValue;
  doc["time"] = log.time;

  String output;
  serializeJson(doc, output);

  ws.textAll(output);

  
}


// create json file "dataLog.json" if it doesn't exist on SD-card
// dataLog should contain the DataLog struct
void createDataLog(){
  File dataLog = SD.open("/dataLog.json", FILE_WRITE);
  if(!dataLog){
    Serial.println("Failed to open dataLog file");
    return;
  }

  // Create a JSON array
  JsonDocument doc;
  JsonArray logArray = doc["log"].to<JsonArray>();

  // Serialize JSON array to file
  if(serializeJson(doc, dataLog) == 0){
    Serial.println("Failed to write to file");
  }

  dataLog.close();
}


// add data to log
void addDataLog(dataLog log){
  
  // First read the JSON file and deserialize it
  File dataLogFile = SD.open("/dataLog.json", "r");
  if(!dataLogFile){
    Serial.println("Failed to open dataLog file");
    return;
  }

  // Create JSON object
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, dataLogFile);
  dataLogFile.close();

  if(error){
    Serial.println("Failed to read file, using default configuration");
    return;
  }

  // Get the array from the document
  JsonArray logArray = doc["log"];

  // Create a new entry
  JsonObject newLogEntry = logArray.add<JsonObject>();
  newLogEntry["accumulatedValue"] = log.accumulatedValue;
  newLogEntry["time"] = log.time;

  // Re-serialize JSON object to file
  dataLogFile = SD.open("/dataLog.json", "w");
  if(!dataLogFile){
    Serial.println("Failed to open dataLog file for writing");
    return;
  }

  if(serializeJson(doc, dataLogFile) == 0){
    Serial.println("Failed to write to file");
  }

  dataLogFile.close();
}


// tasks
// websocket cleanup task
void websocketCleanup( void * pvParameters ){
  while(1){
    ws.cleanupClients();
    vTaskDelay(15000);
  }
}


// add datalog to file task
void handleData( void * pvParameters){
  while(1){
    dataLog log;
    if(xQueueReceive(logQueue, &log, portMAX_DELAY)){
      Serial.println("Handling Queue");
      // take mutex if available
      // then call function addDataLog(dataLog log) to add the log to the file
      if(xSemaphoreTake(SDMutex, portMAX_DELAY) == pdTRUE){
        addDataLog(log);
        xSemaphoreGive(SDMutex);
      }

      // then notify client
      notifyClientSingleLog(log);
      
      
    }
      vTaskDelay(100);
  }

}



void IRAM_ATTR isrImpulse() {
  // Create log
  dataLog log;

  // Get time
  if (getLocalTime(&timeinfo)) {
    // Set time and accumulated value in log
    log.time = mktime(&timeinfo);
  } else {
    log.time = 0; // Failed to get time, use default
  }
  
  log.accumulatedValue = ++accumulatedValue;


  // Send log to queue
  // xQueueSendFromISR(logQueue, &log, NULL);

  // Set pin to LOW
  digitalWrite(interruptPin, LOW);
}


// impulse simulation task
void simulateImpulse( void * pvParameters){
  vTaskDelay(2000);
  while(1){
    // simulate impulse
    // send impulse to isrImpulse
    // send impulse every 80ms
    // send impulse in an interval of 10 seconds
    // send impulse in a random interval of 1-10 impulses
    int randomImpulse = random(1, 10);
    Serial.print("Sending ");
    Serial.print(randomImpulse);
    Serial.println(" impulses");
    for(int i = 0; i < randomImpulse; i++){
      dataLog log;

      // Get time
      struct tm mytimeinfo;
      if (getLocalTime(&mytimeinfo)) {
        // Set time and accumulated value in log
        log.time = mktime(&mytimeinfo);
      } else {
        log.time = 0; // Failed to get time, use default
      }
      
      log.accumulatedValue = ++accumulatedValue;

      Serial.print("Accumulated value: ");
      Serial.println(accumulatedValue);

      Serial.print("Time: ");
      Serial.println(log.time);

      // Send log to queue
      if (xQueueSend(logQueue, &log, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println("Failed to send to queue");
      }
      vTaskDelay(80);
    }
    Serial.println("Impulses sent");
    vTaskDelay(10000);
  }
}

// Delete data log file from SD card
void deleteDataLogFile() {
  if (SD.exists("/dataLog.json")) {
    if (SD.remove("/dataLog.json")) {
      Serial.println("dataLog.json deleted successfully");
      // then create it again
      createDataLog();
      accumulatedValue = 0;
    }
    else {
      Serial.println("Failed to delete dataLog.json");
    }
  }
  else {
    Serial.println("dataLog.json does not exist");
  }
}



// hvad jeg mangler at implementere
// download json fil
// anmod om at nulstille esp32 konfiguration (wifi)
// ændre graf til at være en gauge
// ændre data i graf til at være kwh og ikke impulser ved at finde gennemsnits tiden af hver impuls,
// finde hvor mange gange der vil være en impuls på en time og gange det med 0.001
// style på knapperne
