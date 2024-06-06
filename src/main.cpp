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

// task handles
TaskHandle_t websocketCleanupHandle;
TaskHandle_t handleDataHandle;
TaskHandle_t simulateImpulseHandle;




// prototypes
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


/**
 * @file setup.ino
 * @brief Setup function for initializing various components and configurations.
 *
 * This function initializes the serial communication, sets up the SD card,
 * config file, WiFi, WebSocket, time synchronization, and tasks for data handling.
 *
 * @details
 * The setup function performs the following steps:
 * - Initializes the serial communication at a baud rate of 115200.
 * - Sets a specified pin to low.
 * - Sets up the SD card.
 * - Sets up the configuration file and handles cases where the file is empty or an error occurs.
 * - Sets up the WiFi connection and creates an access point if the connection fails.
 * - Initializes the WebSocket and adds routes.
 * - Synchronizes time using NTP server.
 * - Creates a queue and a mutex for handling data logging.
 * - Creates and starts tasks for WebSocket cleanup, data handling, and impulse simulation.
 *
 * @note Ensure to define the necessary global variables and functions such as `interruptPin`, `setupSD()`, `setupConfig()`, 
 * `createAccessPoint()`, `setupWifi()`, `websocketInit()`, `addRoutes()`, `gmtOffset_sec`, `daylightOffset_sec`, 
 * `ntpServer`, `getLocalTime()`, `logQueue`, `xQueueCreate()`, `xSemaphoreCreateMutex()`, `websocketCleanupHandle`, 
 * `handleDataHandle`, `simulateImpulseHandle`, `websocketCleanup()`, `handleData()`, and `simulateImpulse()`.
 *
 * @return void
 */
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
  logQueue = xQueueCreate(1024, sizeof( struct dataLog));
  SDMutex = xSemaphoreCreateMutex();


  // setup tasks
  xTaskCreate(websocketCleanup, "websocketCleanup", 2048, NULL, 1, &websocketCleanupHandle);
  xTaskCreate(handleData, "handleData", 4096, NULL, 2, &handleDataHandle);
  xTaskCreate(simulateImpulse, "simulateImpulse", 2048, NULL, 3, &simulateImpulseHandle);

  vTaskDelay(1000);

}


void loop() {

}


/**
 * @brief Connects to the WiFi network using predefined configurations.
 *
 * This function sets up the WiFi connection using the provided SSID and password
 * from the `config` structure. It attempts to connect to the WiFi and sets up
 * MDNS responder for local network services.
 *
 * @details
 * The function performs the following steps:
 * - Configures the WiFi with a static IP, gateway, subnet, and DNS.
 * - Begins the WiFi connection with the provided SSID and password.
 * - Waits for the WiFi to connect, with a timeout of 30 seconds.
 * - Sets up the MDNS responder if the connection is successful.
 * - Prints connection status and MDNS address to the serial monitor.
 *
 * @return 
 * - `true` if the WiFi connection and MDNS setup are successful.
 * - `false` if the connection fails or MDNS setup fails.
 */
bool setupWifi(){
  IPAddress ip;

  WiFi.config(config.ip, config.gateway, subnet, dns);
  WiFi.begin(config.ssid, config.password);
  Serial.print("Connecting to WiFi.."); 
  int i = 0;
  bool didConnect = true;
  while (WiFi.status() != WL_CONNECTED) {
    if(i >= 30)
    {
      Serial.println("Failed to connect to WiFi");
      didConnect = false;
      break;
    }
    vTaskDelay(1000);
    Serial.print(".");
    i++;
  }
  if(!didConnect){
    return false;
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


/**
 * @brief Initializes the SD card and handles the dataLog.json file.
 *
 * This function checks for the presence of an SD card, mounts it, and ensures that
 * a dataLog.json file exists. If the file doesn't exist, it creates a new one.
 * If the file exists, it reads and parses the JSON content to retrieve the last
 * accumulated value.
 *
 * @details
 * The function performs the following steps:
 * - Attempts to mount the SD card.
 * - Checks the type of the SD card.
 * - Verifies the existence of dataLog.json file.
 * - Creates dataLog.json file if it doesn't exist.
 * - Reads and parses the existing dataLog.json file if it exists.
 * - Extracts the last accumulated value from the JSON content if available.
 *
 * @note This function assumes the presence of `createDataLog()`, `accumulatedValue`,
 * and necessary libraries such as `SD`, `File`, and `JsonDocument`.
 *
 * @return void
 */

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


/**
 * @brief Sets up the configuration from a JSON file.
 *
 * This function initializes LittleFS, reads the configuration file "config.json",
 * and sets up the configuration parameters such as SSID, password, IP address,
 * and gateway from the JSON content. It returns an integer value indicating the
 * setup status.
 *
 * @details
 * The function performs the following steps:
 * - Initializes LittleFS to access the filesystem.
 * - Opens the "config.json" file on LittleFS for reading.
 * - Deserializes the JSON content of the file into a JSON document.
 * - Copies the SSID, password, IP address, and gateway from the JSON document
 *   to the configuration structure.
 * - Closes the file after reading.
 * - Checks if the configuration parameters are empty by verifying if the first
 *   element of each parameter array is null.
 *
 * @note This function assumes the presence of the "config.json" file on LittleFS,
 * the configuration structure (`config`), and LittleFS filesystem. Ensure that the
 * file exists and has the correct format before calling this function.
 *
 * @return An integer value indicating the setup status:
 * - 0: Failed to initialize LittleFS or deserialize JSON.
 * - 1: Configuration file is empty.
 * - 2: Configuration successfully read and set up.
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
    Serial.println("Failed to open config file creating new one...");
    // create new config.json
    configFile = LittleFS.open("/config.json", "w");
    if(!configFile){
      Serial.println("Failed to create config file");
      return 0;
    }
    // write empty struct config to json file
    JsonDocument doc;
    doc["ssid"] = "";
    doc["password"] = "";
    doc["ip"] = "";
    doc["gateway"] = "";

    if(serializeJson(doc, configFile) == 0){
      Serial.println("Failed to write to file");
    }

    configFile.close();
    // list all files in the filesystem
    Serial.println("Filesystem content:");
    File root = LittleFS.open("/");
    File file = root.openNextFile();
    while(file){
      Serial.print("  FILE: ");
      Serial.println(file.name());
      file = root.openNextFile();
    }

    return 1;
  }

  // read config file and set ssid and password
  // from the arduinoJson.h library examples: https://arduinojson.org/v6/example/config/
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if(error){
    Serial.println("Failed to deserialize Json");
    return 1;
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

  Serial.println(config.ip);


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


/**
 * @brief Saves the current configuration to a JSON file.
 *
 * This function writes the WiFi SSID, password, IP, and gateway from the global 
 * `config` structure to the `/config.json` file using the LittleFS filesystem.
 *
 * @details
 * The function performs the following steps:
 * - Opens the `/config.json` file for writing.
 * - Creates a JSON object and populates it with the configuration values.
 * - Serializes the JSON object and writes it to the file.
 * - Closes the file after writing.
 *
 * @note This function assumes the presence of `config` structure, `LittleFS`, 
 * `File`, and `JsonDocument`. Ensure these are properly defined and included in your code.
 *
 * @return void
 */
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


/**
 * @brief Sets up the ESP device as a WiFi Access Point and configures HTTP routes.
 *
 * This function sets up the ESP device as a WiFi Access Point with the SSID "Energy_Collector_Wifi".
 * It configures the IP address and sets up HTTP routes for serving HTML content and handling
 * HTTP POST requests to update the WiFi configuration.
 *
 * @details
 * The function performs the following steps:
 * - Sets the device as a WiFi Access Point.
 * - Configures the IP address for the Access Point.
 * - Sets up an HTTP GET route to serve the WiFi manager HTML page.
 * - Sets up an HTTP POST route to handle form submissions, updating the SSID, password, IP, and gateway.
 * - Saves the updated configuration and restarts the ESP device.
 *
 * @note This function assumes the presence of `config` structure, `LittleFS`, `AsyncWebServer`, and necessary
 * libraries. Ensure these are properly defined and included in your code.
 *
 * @return void
 */
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


/**
 * @brief Initializes the WebSocket server and sets the event handler.
 *
 * This function sets the event handler for the WebSocket server and adds the
 * WebSocket server as a handler to the main HTTP server.
 *
 * @details
 * The function performs the following steps:
 * - Sets the event handler for WebSocket events using the `onEvent` function.
 * - Adds the WebSocket server as a handler to the main HTTP server.
 *
 * @note This function assumes the presence of `ws` (WebSocket server) and `server` 
 * (HTTP server) objects. Ensure these are properly defined and included in your code.
 *
 * @return void
 */
void websocketInit(){
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}


/**
 * @brief Handles WebSocket events for the server.
 *
 * This function processes various WebSocket events such as connect, disconnect,
 * and data reception. It logs client connections and disconnections, sends the 
 * entire log to newly connected clients, and handles incoming data.
 *
 * @param server Pointer to the WebSocket server instance.
 * @param client Pointer to the WebSocket client instance.
 * @param type The type of WebSocket event.
 * @param arg Additional arguments for the event.
 * @param data Pointer to the data received.
 * @param len Length of the data received.
 *
 * @details
 * The function handles the following WebSocket events:
 * - `WS_EVT_CONNECT`: Logs the connection and sends the entire log to the newly connected client.
 * - `WS_EVT_DISCONNECT`: Logs the disconnection.
 * - `WS_EVT_DATA`: Handles incoming data using the `handleWebSocketEvent` function.
 * - `WS_EVT_PONG` and `WS_EVT_ERROR`: Currently no actions are taken for these events.
 *
 * @note This function assumes the presence of `sendLogToClient` and `handleWebSocketEvent` 
 * functions. Ensure these are properly defined and included in your code.
 *
 * @return void
 */
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len){

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


/**
 * @brief Sends the data log stored in the dataLog.json file to a WebSocket client.
 *
 * This function reads the dataLog.json file from the SD card, parses its content
 * into a JSON object, serializes the JSON object into a string, and sends it to
 * the specified WebSocket client.
 *
 * @param client Pointer to the WebSocket client instance.
 *
 * @details
 * The function performs the following steps:
 * - Opens the dataLog.json file from the SD card for reading.
 * - Parses the JSON content into a JSON object.
 * - Serializes the JSON object into a string.
 * - Sends the string containing the JSON data to the WebSocket client.
 *
 * @note This function assumes the presence of the SD card with the dataLog.json file,
 * the JSON content in the correct format, and the WebSocket client instance.
 * Ensure these conditions are met and properly defined in your code.
 *
 * @return void
 */
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
    Serial.println("Failed to read datalog file, using default configuration");
    return;
  }

  // Serialize JSON object to string
  String output;
  serializeJson(doc, output);

  // Send JSON object to the connected client
  client->text(output);
}


/**
 * @brief Adds HTTP routes to the AsyncWebServer instance.
 *
 * This function configures various HTTP routes for serving HTML, CSS, JavaScript, and
 * JSON files, as well as handling specific HTTP POST requests. It serves static files
 * stored in the LittleFS filesystem and provides endpoints for downloading the dataLog.json
 * file and entering configuration mode.
 *
 * @details
 * The function performs the following steps:
 * - Sets up an HTTP GET route to serve the index.html file.
 * - Serves static files (index.html, style.css, and script.js) stored in the LittleFS filesystem.
 * - Configures an HTTP GET route to download the dataLog.json file from the SD card.
 * - Defines an HTTP POST route to enter configuration mode, suspends tasks, disconnects from WiFi,
 *   and creates an access point.
 * - Begins serving the HTTP routes.
 *
 * @note This function assumes the presence of the `server`, `LittleFS`, and `SD` objects,
 * as well as necessary files in the LittleFS filesystem and the dataLog.json file on the SD card.
 * Ensure these conditions are met and properly defined in your code.
 *
 * @return void
 */
void addRoutes() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(LittleFS, "/script.js", "text/javascript");
  });

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/dataLog.json", "application/json");
  });

  server.on("/configMode", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", "Entering configuration mode");
    vTaskDelay(1000);
    // stop all tasks
    vTaskSuspend(websocketCleanupHandle);
    vTaskSuspend(handleDataHandle);
    vTaskSuspend(simulateImpulseHandle);

    // set config file to empty data
    File configFile = LittleFS.open("/config.json", "w");
    if(!configFile){
      Serial.println("Failed to open config file for writing");
      return;
    }
    // create json object
    JsonDocument doc;
    doc["ssid"] = "";
    doc["password"] = "";
    doc["ip"] = "";
    doc["gateway"] = "";

    // serialize json object to file
    if(serializeJson(doc, configFile) == 0){
      Serial.println("Failed to write to file");
    }

    configFile.close();
    
    // restart esp
    ESP.restart();
  });

  server.begin();
}


/**
 * @brief Handles WebSocket events and processes incoming data.
 *
 * This function parses incoming WebSocket data into a JSON object and 
 * checks for specific requests from the client. It processes requests 
 * such as requesting the entire log, requesting a single log entry, 
 * and requesting to delete the data log file.
 *
 * @param arg Pointer to additional arguments for the event (not used).
 * @param data Pointer to the incoming data.
 * @param len Length of the incoming data.
 *
 * @details
 * The function performs the following steps:
 * - Parses the incoming WebSocket data into a JSON object.
 * - Checks for specific requests from the client:
 *   - "wholeLog": Requests the entire log. Calls `notifyClientWholeLog` function.
 *   - "singleLog": Requests a single log entry. Calls `notifyClientSingleLog` function.
 *   - "deleteDataLogFile": Requests to delete the data log file. Calls `deleteDataLogFile` function.
 *
 * @note This function assumes the presence of the `JsonDocument`, `notifyClientWholeLog`, 
 * `notifyClientSingleLog`, and `deleteDataLogFile` functions. Ensure these functions are 
 * properly defined and included in your code.
 *
 * @return void
 */
void handleWebSocketEvent(void *arg, uint8_t *data, size_t len){

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


/**
 * @brief Sends the entire data log stored in the dataLog.json file to all connected WebSocket clients.
 *
 * This function reads the dataLog.json file from the SD card, parses its content into a JSON object,
 * serializes the JSON object into a string, and sends it to all connected WebSocket clients.
 *
 * @details
 * The function performs the following steps:
 * - Opens the dataLog.json file from the SD card for reading.
 * - Parses the JSON content into a JSON object.
 * - Serializes the JSON object into a string.
 * - Sends the string containing the JSON data to all connected WebSocket clients using the WebSocket server.
 *
 * @note This function assumes the presence of the SD card with the dataLog.json file,
 * the JSON content in the correct format, and the WebSocket server instance.
 * Ensure these conditions are met and properly defined in your code.
 *
 * @return void
 */
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
    Serial.println("Failed to read datalog file, using default configuration");
    return;
  }

  // Serialize JSON object to string
  String output;
  serializeJson(doc, output);

  // Send JSON object to all connected clients
  ws.textAll(output);
}


/**
 * @brief Sends a single data log entry to all connected WebSocket clients.
 *
 * This function creates a JSON object containing a single data log entry with 
 * accumulated value and time, serializes the JSON object into a string, and 
 * sends it to all connected WebSocket clients.
 *
 * @param log The data log entry to be sent to clients.
 *
 * @details
 * The function performs the following steps:
 * - Creates a JSON object containing the accumulated value and time from the given log.
 * - Serializes the JSON object into a string.
 * - Sends the string containing the JSON data to all connected WebSocket clients using the WebSocket server.
 *
 * @note This function assumes the presence of the `dataLog` structure and the WebSocket server instance.
 * Ensure these conditions are met and properly defined in your code.
 *
 * @param log The data log entry to be sent to clients.
 * @return void
 */
void notifyClientSingleLog(dataLog log){
  JsonDocument doc;
  doc["accumulatedValue"] = log.accumulatedValue;
  doc["time"] = log.time;

  String output;
  serializeJson(doc, output);

  ws.textAll(output);

  
}


/**
 * @brief Creates a new data log file on the SD card with an empty JSON array.
 *
 * This function creates a new data log file named "dataLog.json" on the SD card
 * and initializes it with an empty JSON array structure.
 *
 * @details
 * The function performs the following steps:
 * - Opens the "dataLog.json" file on the SD card for writing.
 * - Creates a JSON document and initializes an empty JSON array for the log data.
 * - Serializes the JSON array into the file.
 * - Closes the file after writing.
 *
 * @note This function assumes the presence of the SD card and proper initialization.
 * Ensure that the SD card is properly initialized and accessible before calling this function.
 *
 * @return void
 */
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


/**
 * @brief Adds a new data log entry to the existing data log file on the SD card.
 *
 * This function adds a new data log entry to the existing "dataLog.json" file
 * on the SD card. It first reads the JSON file, deserializes it into a JSON object,
 * adds the new log entry to the JSON array, and re-serializes the JSON object back
 * to the file.
 *
 * @param log The data log entry to be added to the log file.
 *
 * @details
 * The function performs the following steps:
 * - Opens the "dataLog.json" file on the SD card for reading.
 * - Deserializes the JSON content into a JSON object.
 * - Gets the JSON array containing log entries from the JSON object.
 * - Adds a new log entry to the JSON array.
 * - Re-serializes the JSON object and writes it back to the file.
 *
 * @note This function assumes the presence of the SD card and the "dataLog.json" file.
 * Ensure that the file exists and has the correct format before calling this function.
 *
 * @param log The data log entry to be added to the log file.
 * @return void
 */
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
    Serial.println("Failed to read datalog file, using default configuration");
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


/**
 * @brief Cleans up WebSocket clients periodically.
 *
 * This task periodically cleans up WebSocket clients by removing any disconnected clients
 * from the WebSocket server's client list. It is intended to be run as a separate FreeRTOS
 * task to ensure that WebSocket clients are properly managed.
 *
 * @param pvParameters A pointer to task parameters (not used).
 *
 * @details
 * The function performs the following steps:
 * - Enters an infinite loop to continuously clean up WebSocket clients.
 * - Calls the `cleanupClients` function of the WebSocket server to remove disconnected clients.
 * - Delays the task execution for a specified interval (15 seconds in this case) using vTaskDelay.
 *
 * @note This function assumes the presence of the WebSocket server instance (`ws`) and FreeRTOS.
 * Ensure that the WebSocket server is properly initialized and FreeRTOS is configured before
 * calling this function.
 *
 * @param pvParameters A pointer to task parameters (not used).
 * @return void
 */
void websocketCleanup( void * pvParameters ){
  while(1){
    ws.cleanupClients();
    vTaskDelay(15000);
  }
}


/**
 * @brief Handles incoming data logs from a queue and updates the data log file.
 *
 * This task continuously waits for incoming data logs from a queue and handles them
 * by adding the logs to the data log file on the SD card and notifying WebSocket clients
 * about the new log entry.
 *
 * @param pvParameters A pointer to task parameters (not used).
 *
 * @details
 * The function performs the following steps:
 * - Enters an infinite loop to continuously handle incoming data logs.
 * - Waits for a data log to be received from the queue using xQueueReceive.
 * - Upon receiving a data log, adds the log to the data log file by calling the `addDataLog` function.
 * - Notifies WebSocket clients about the new log entry by calling the `notifyClientSingleLog` function.
 * - Ensures mutual exclusion while accessing the SD card by using a semaphore.
 * - Delays the task execution for a specified interval (100 milliseconds in this case) using vTaskDelay.
 *
 * @note This function assumes the presence of the data log queue (`logQueue`), the SD card, the
 * `addDataLog` and `notifyClientSingleLog` functions, and FreeRTOS. Ensure that the queue is properly
 * initialized, the SD card is accessible, and FreeRTOS is configured before calling this function.
 *
 * @param pvParameters A pointer to task parameters (not used).
 * @return void
 */
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


/**
 * @brief Simulates impulses and sends them to the data log queue.
 *
 * This task simulates impulses by generating a random number of impulses between
 * 20 and 40, and then sends each impulse to the data log queue with a time interval
 * of approximately 80 milliseconds between impulses. The task runs indefinitely.
 *
 * @param pvParameters A pointer to task parameters (not used).
 *
 * @details
 * The function performs the following steps:
 * - Delays the task execution for 2000 milliseconds to allow initialization.
 * - Enters an infinite loop to continuously simulate impulses.
 * - Generates a random number of impulses between 20 and 40.
 * - Calculates the time interval between impulses based on the total time (10 seconds)
 *   divided by the random number of impulses.
 * - Sends each impulse to the data log queue with an accumulated value and timestamp.
 * - Delays the task execution for the calculated time interval between impulses.
 *
 * @note This function assumes the presence of the data log queue (`logQueue`), the `accumulatedValue`
 * variable, the `getLocalTime` function, and FreeRTOS. Ensure that the queue is properly initialized,
 * the `getLocalTime` function is correctly implemented, and FreeRTOS is configured before calling
 * this function.
 *
 * @param pvParameters A pointer to task parameters (not used).
 * @return void
 */
void simulateImpulse( void * pvParameters){
  vTaskDelay(2000);
  while(1){
    // simulate impulse
    // send impulse to isrImpulse
    // send impulse every 80ms
    // send impulse in an interval of 10 seconds
    // send impulse in a random interval of 1-10 impulses
    int randomImpulse = random(20, 40);
    int timePerImpulse = 10000 / randomImpulse;
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
      vTaskDelay(timePerImpulse);
    }
  }
}


/**
 * @brief Deletes the data log file and recreates it.
 *
 * This function deletes the "dataLog.json" file from the SD card if it exists,
 * and then recreates the file with an empty JSON array structure. It also resets
 * the accumulated value to zero.
 *
 * @details
 * The function performs the following steps:
 * - Checks if the "dataLog.json" file exists on the SD card.
 * - If the file exists, attempts to delete it using the SD library's remove function.
 * - If deletion is successful, prints a success message and recreates the file using the `createDataLog` function.
 * - If deletion fails, prints an error message.
 * - If the file does not exist, prints a message indicating that the file does not exist.
 *
 * @note This function assumes the presence of the SD card and the `createDataLog` function.
 * Ensure that the SD card is properly initialized and accessible before calling this function.
 *
 * @return void
 */
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
