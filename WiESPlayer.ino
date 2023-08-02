#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <ESPmDNS.h>
#include <Update.h>

//Constants
const char* AP_SSID = "ESP32";
const char* AP_PASSWORD = "12345678";
const char* HOSTNAME = "esp32";
const char* serverIndex = "<form method='POST' action='/update' enctype='multipart/form-data'>"
                          "<input type='file' name='update'><input type='submit' value='Update'></form>";
//Server
AsyncWebServer server(80);

// Function prototypes
void createWiFi_AP();
void configOTA();

void setup() {
  createWiFi_AP();
  MDNS.begin(HOSTNAME);
  configOTA();
  MDNS.addService("http", "tcp", 80);
  
}

void loop() {
  delay(2); 
}

void createWiFi_AP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

void configOTA() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", serverIndex);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    int code = Update.hasError() ? 500 : 200;
    request->send(code, "text/plain", Update.hasError() ? "FAIL" : "OK");
    delay(100);
    ESP.restart();
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!index) {
      if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
        Update.printError(Serial);
      }
    }
    if (!Update.hasError()) {
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
    }
    if (final) {
      if (!Update.end(true)) {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}
