#include <Arduino.h>



void setup() {
  Serial.begin(115200);

}

void loop() {

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