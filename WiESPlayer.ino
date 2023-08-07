#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebSrv.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include "Audio.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include "RTClib.h"

// Digital I/O used
#define SD_CS          5
#define SPI_MOSI      23
#define SPI_MISO      19
#define SPI_SCK       18
#define I2S_DOUT      25
#define I2S_BCLK      27
#define I2S_LRC       26

// Defines
#define EEPROM_SIZE 256
#define GLOABAL_SSID_ADDR 0
#define GLOABAL_PASSWORD_ADDR 100
#define MAX_ATTEMPTS 20            // How many repeates do WIFi connection
#define SPI_FREQ 10000000

#define MAX_TRACKS 100
String tracks[MAX_TRACKS];
int trackCount = 0;
int currentTrack = 0;

// Constants
const char* AP_SSID = "ESP32";
const char* AP_PASSWORD = "12345678";
const char* HOSTNAME = "esp32";

// Global variables
int currentVolume = 5; // Starting volume of audio player
bool flagNextSong = false; 
bool flagPrevSong = false; 

// Player modes
bool flagModeAlarm = false; 

// Alarm time
int alarmHour = 0;
int alarmMinute = 0;

// Server
AsyncWebServer server(80);

Audio audio;
File rootDirectory;
HTTPClient httpClient;
RTC_DS3231 rtc;
DateTime now;

// Function prototypes
void createWiFi_AP(void);
bool connectToGlobalWiFi(const char* ssid, const char* password);
String readStringEEPROM(int addr);
void writeStringEEPROM(int addr, String data);
bool isMusicFile(String fileName);
void configOTAPage(void);
void configGlobalWiFiPage(void);
void configMainPage(void);
void configPlaylistPage(void);
void configClockPage(void); 

void setup() {
  rtc.begin();
  pinMode(SD_CS, OUTPUT);      
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(SPI_FREQ);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  while (!SD.begin(SD_CS));

  EEPROM.begin(EEPROM_SIZE);
  if(!connectToGlobalWiFi(readStringEEPROM(GLOABAL_SSID_ADDR).c_str(), 
                          readStringEEPROM(GLOABAL_PASSWORD_ADDR).c_str())) {
    createWiFi_AP();
  }
  MDNS.begin(HOSTNAME);
  
  configOTAPage();
  configGlobalWiFiPage(GLOABAL_SSID_ADDR, GLOABAL_PASSWORD_ADDR);
  configMainPage(); 
  configPlaylistPage();
  configClockPage();
  server.begin();
  
  MDNS.addService("http", "tcp", 80);
  audio.setVolume(currentVolume); // 0...21

  rootDirectory = SD.open("/");
  File file = rootDirectory.openNextFile();
  while(file && trackCount < MAX_TRACKS) {
    if (isMusicFile(file.name())) {
      tracks[trackCount++] = String(file.name());
    }
    file = rootDirectory.openNextFile();
  }
  audio.connecttoSD(tracks[currentTrack].c_str());
}

void loop() {
    DateTime now = rtc.now();
  // Check if it's time to alarm
  if(flagModeAlarm && now.hour() == alarmHour && now.minute() == alarmMinute){
    audio.setVolume(15);
    flagModeAlarm = false;
  }
  if (flagNextSong) {
    currentTrack = (currentTrack + 1) % trackCount;
    audio.connecttoSD(tracks[currentTrack].c_str());
    flagNextSong = false; 
  }
  if (flagPrevSong){
    currentTrack = (currentTrack - 1 + trackCount) % trackCount;
    audio.connecttoSD(tracks[currentTrack].c_str());
    flagPrevSong = false; 
  }
  audio.loop();
}

void audio_eof_mp3(const char *info) {
  if (trackCount > 0 ) {
    currentTrack = (currentTrack + 1) % trackCount; // loop back to the first track after the last one
    audio.connecttoSD(tracks[currentTrack].c_str());  // start playing the next track
  }
}
// void audio_info(const char *info) {
//   Serial.print("info        "); 
//   Serial.println(info);
// }

bool isMusicFile(String fileName) {
  String extension = fileName.substring(fileName.lastIndexOf('.') + 1);
  extension.toLowerCase();
  return (extension == "mp3" || extension == "MP3");
}

bool connectToGlobalWiFi(const char* ssid, const char* password) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  int attemptCount = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    attemptCount++;
    if(attemptCount > MAX_ATTEMPTS) return false; 
  }
  return true;
}

void createWiFi_AP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
}

const char* update_page = "<form method='POST' action='/update' enctype='multipart/form-data'>"
                          "<input type='file' name='update'><input type='submit' value='Update'></form>";

void configOTAPage() {
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", update_page);
    audio.pauseResume();  // Mute audio here before starting the update
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
}

void configGlobalWiFiPage (int ssid_addr, int pass_addr) {
  server.on("/wifi", HTTP_GET, [](AsyncWebServerRequest *request){
    String s = "<html><body>";
    s += "<form method='post' action='/wifi'>";
    s += "SSID:<br><input type='text' name='ssid'><br>";
    s += "Password:<br><input type='text' name='password'><br>";
    s += "<input type='submit' value='Submit'>";
    s += "</form></body></html>";
    request->send(200, "text/html", s);
  });
  server.on("/wifi", HTTP_POST, [=](AsyncWebServerRequest *request){
    String ssid;
    String password;
    if(request->hasParam("ssid", true)) {
      ssid = request->getParam("ssid", true)->value();
      writeStringEEPROM(ssid_addr, ssid);
    }
    if(request->hasParam("password", true)){
      password = request->getParam("password", true)->value();
      writeStringEEPROM(pass_addr, password);
    }
    ESP.restart();
  });
}
const char* playlist_page PROGMEM = R"rawliteral(
<a href="/">Home page</a><br/>
<a href="/update">Update Firmware</a>
<form id="upload_form" action="/upload" method="POST" enctype="multipart/form-data">
    <input type="file" id="file" name="upload">
    <input type="submit" value="Upload">
</form>
<div id="progress">Progress: 0%</div>
<script>
document.getElementById("upload_form").addEventListener("submit", function(event){
    event.preventDefault();
    var fileInput = document.getElementById("file");
    var file = fileInput.files[0];
    var formData = new FormData();
    formData.append("upload", file);
    
    var xhr = new XMLHttpRequest();
    xhr.open("POST", "/upload", true);
    
    xhr.upload.onprogress = function(event) {
        if(event.lengthComputable) {
            var progress = Math.round((event.loaded / event.total) * 100);
            document.getElementById("progress").innerHTML = "Progress: " + progress + "%";
        }
    };
    
    xhr.send(formData);
});
</script>
)rawliteral";


void listDir(File dir, String& fileList) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    if (isMusicFile(entry.name())) {
      fileList += entry.name();
      fileList += " <a href=\"/delete?file=";
      fileList += entry.name();
      fileList += "\">Remove</a><br>";
    }
    entry.close();
  }
}
void configPlaylistPage() {
  // Receive and save the file on POST request
  server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200); 
  },[](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
      if(!index){
        rootDirectory = SD.open("/" + filename, FILE_WRITE);
      }
      rootDirectory.write(data, len);
      if(final){
        if (isMusicFile(filename) && trackCount < MAX_TRACKS) {
          tracks[trackCount++] = filename;
        }
        rootDirectory.close();
      }
  });
  server.on("/playlist", HTTP_GET, [](AsyncWebServerRequest *request){
    String fileList = "<html><body>";
    File root = SD.open("/");
    listDir(root, fileList);
    String completePage = String(playlist_page) + "<br/>" + fileList + "</body></html>";
    request->send(200, "text/html", completePage);
  });
  // Remove file
  server.on("/delete", HTTP_GET, [](AsyncWebServerRequest *request) {
    if(request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      if(SD.remove("/" + filename)) {
        request->send(200, "text/plain", "File deleted");
      } else {
        request->send(500, "text/plain", "Delete failed");
      }
    } else {
      request->send(400, "text/plain", "Bad request");
    }
  });
}

const char main_page[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<body>
<button id="prevSongButton" type="button">PrevSong</button>
<button id="playStopButton" type="button">Play/Stop</button>
<button id="nextSongButton" type="button">NextSong</button>
<br/> 
<label id="volumeLabel1" style="margin-right: 20px;">Volume: 10</label>
<input type="range" min="0" max="21" value="10" id="volumeSlider1">
<br/>
<a href="/playlist">Playlist</a><br/>
<a href="/time">Time and Alarm</a><br/>
<a href="/wifi">Wi-Fi settings</a><br/>
<a href="/update">Update Firmware</a>
<script>
document.getElementById("prevSongButton").addEventListener("click", function(){
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/prev_song", true);
    xhr.send();
});

document.getElementById("playStopButton").addEventListener("click", function(){
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/play_stop", true);
    xhr.send();
});

document.getElementById("nextSongButton").addEventListener("click", function(){
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/next_song", true);
    xhr.send();
});

document.getElementById("volumeSlider1").addEventListener("input", function(){
    var xhr = new XMLHttpRequest();
    xhr.open("GET", "/set_volume1?volume=" + this.value, true);
    xhr.send();
    document.getElementById("volumeLabel1").innerHTML = "Volume: " + this.value;
});

</script>
</body>
</html>
)rawliteral";

void configMainPage() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", main_page);
  });
    // Set the flag when the NextSong button is pressed
  server.on("/next_song", HTTP_GET, [](AsyncWebServerRequest *request){
    flagNextSong = true;
    request->send(200);
  });

    // Set the flag when the PrevSong button is pressed
  server.on("/prev_song", HTTP_GET, [](AsyncWebServerRequest *request){
    flagPrevSong = true;
    request->send(200);
  });

  // Toggle the state when the Play/Stop button is pressed
  server.on("/play_stop", HTTP_GET, [](AsyncWebServerRequest *request){
      audio.pauseResume();
      request->send(200);
  });

  // Adjust volume when the volume slider changes
  server.on("/set_volume1", HTTP_GET, [](AsyncWebServerRequest *request){
      if(request->hasParam("volume")) {
          currentVolume = request->getParam("volume")->value().toInt();
          audio.setVolume(currentVolume);
          request->send(200);
      } else {
          request->send(400, "text/plain", "Bad request");
      }
  });
}
const char* clock_page PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<body>
  <h3>Current Time: <span id="currentTime"></span></h3>
  <h3>Alarm Time: <span id="alarmTime"></span></h3>
  <button id="activAlarm" type="button">Alarm On/Off</button>
  <h4>Set Alarm</h4>
  <form action="/setAlarm">
    <input type="text" name="alarmHour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="alarmMinute" placeholder="Minute" style="width: 40px;">
    <input type="submit" value="Set Alarm">
  </form>

  <h4>Set Time</h4>
  <form action="/setTime">
    <input type="text" name="hour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="minute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="second" placeholder="Second" style="width: 50px;">
    <input type="submit" value="Set Time">
  </form> 

  <script>
    document.getElementById("activAlarm").addEventListener("click", function(){
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/activAlarm", true);
      xhr.send();
    });

    function updateTime() {
      fetch('/getTime')
        .then(response => response.text())
        .then(data => {
          document.getElementById('currentTime').textContent = data;
        });
    }
    function updateAlarm() {
      fetch('/getAlarm')
        .then(response => response.text())
        .then(data => {
          document.getElementById('alarmTime').textContent = data;
        });
    }
    
    // Update the time immediately, and then every second
    updateTime();
    setInterval(updateTime, 60000);
    updateAlarm(); 
  </script>

</body>
</html>
)rawliteral"; 

void configClockPage(){
  // Serve the HTML
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", clock_page);
  });
    // Toggle the state when the Play/Stop button is pressed
  server.on("/activAlarm", HTTP_GET, [](AsyncWebServerRequest *request){
      flagModeAlarm = (flagModeAlarm) ? false: true; 
      request->send(200);
  });
// Handle setting the time
server.on("/setTime", HTTP_GET, [] (AsyncWebServerRequest *request) {
  int hour, minute, second;
  int params = request->params();
  for(int i=0;i<params;i++){
    AsyncWebParameter* p = request->getParam(i);
    if(p->name()=="hour"){
      hour = p->value().toInt();
    }
    if(p->name()=="minute"){
      minute = p->value().toInt();
    }
    if(p->name()=="second"){
      second = p->value().toInt();
    }
  }
  DateTime newTime = DateTime(now.year(), now.month(), now.day(), hour, minute, second);
  rtc.adjust(newTime);
  request->send(200, "text/plain", "OK");
});

  // Handle setting the alarm
  server.on("/setAlarm", HTTP_GET, [] (AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->name()=="alarmHour"){
        alarmHour = p->value().toInt();
      }
      if(p->name()=="alarmMinute"){
        alarmMinute = p->value().toInt();
      }
    }
    flagModeAlarm = true;
    request->send(200, "text/plain", "OK");
  });
  server.on("/getTime", HTTP_GET, [] (AsyncWebServerRequest *request) {
  DateTime now = rtc.now();
  String currentTime = String(now.hour()) + ":" + String(now.minute());
  request->send(200, "text/plain", currentTime);
  });
  server.on("/getAlarm", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String alarmTime = String(alarmHour) + ":" + String(alarmMinute);
    request->send(200, "text/plain", alarmTime);
  });

}

/* Function to write in EEPROM string data */ 
void writeStringEEPROM(int addr, String data) {
  int len = data.length();
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + 1 + i, data[i]);
  }
  EEPROM.commit();
}
/* Function to read string from EEPROM by address */ 
String readStringEEPROM(int addr) {
  int len = EEPROM.read(addr);
  String data = "";
  for (int i = 0; i < len; i++) {
    data = data + (char)EEPROM.read(addr + 1 + i);
  }
  return data;
}