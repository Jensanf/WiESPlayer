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
#define VOLUME_ADDR 200
#define ALARM_HOUR_ADDR 201
#define ALARM_MIN_ADDR 202
#define ALARM_FLAG_ADDR 203
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
void deleteTrack(String tracksList[], int &trackCount, const String &trackName); 

void setup() {
  rtc.begin();
  pinMode(SD_CS, OUTPUT);      
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(SPI_FREQ);

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  
  while (!SD.begin(SD_CS));

  EEPROM.begin(EEPROM_SIZE);
  currentVolume = EEPROM.read(VOLUME_ADDR);
  alarmHour = EEPROM.read(ALARM_HOUR_ADDR);
  alarmMinute = EEPROM.read(ALARM_MIN_ADDR);
  flagModeAlarm = EEPROM.read(ALARM_FLAG_ADDR);

  currentVolume = (currentVolume > 21 || currentVolume < 0) ? 5 : currentVolume;
  alarmHour = (alarmHour > 24 || alarmHour < 0) ? 0 : alarmHour;
  alarmMinute = (alarmMinute > 59 || alarmMinute < 0) ? 0 : alarmMinute;
  flagModeAlarm = (flagModeAlarm > 1 || flagModeAlarm < 0) ? 0 : flagModeAlarm;

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
  currentTrack = esp_random() % trackCount; // random(0, trackCount);
  audio.connecttoSD(tracks[currentTrack].c_str());
}

void loop() {
  DateTime now = rtc.now();
  // Check if it's time to alarm
  if (flagModeAlarm && now.hour() == alarmHour && now.minute() == alarmMinute) {
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

const char* update_page = "<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>"
                          "<form method='POST' action='/update' enctype='multipart/form-data'>"
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
    s += "<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>";
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
<a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>
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
    xhr.onload = function() {
        if (this.status == 200 && this.responseText == "refresh") {
            location.reload();  
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
        audio.pauseResume();
        rootDirectory = SD.open("/" + filename, FILE_WRITE);
      }
      rootDirectory.write(data, len);
      if(final){
        if (isMusicFile(filename) && trackCount < MAX_TRACKS) {
          tracks[trackCount++] = filename;
          audio.pauseResume();
        }
        request->send(200, "text/plain", "refresh"); 
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
        deleteTrack(tracks, trackCount, filename);
      } else {
        request->send(500, "text/plain", "Delete failed");
      }
    } else {
      request->send(400, "text/plain", "Bad request");
    }
  });
}

const char main_page_start[] PROGMEM= R"rawliteral(
<!DOCTYPE HTML><html>
<body>
<b>Now Playing: <span id="currentTrack"></span></b><br/><br/>
<button id="prevSongButton" type="button">PrevSong</button>
<button id="playStopButton" type="button">Play/Stop</button>
<button id="nextSongButton" type="button">NextSong</button> <br/><br/> 
)rawliteral";

const char main_page_end[] PROGMEM= R"rawliteral(
<br/>
<a href="/playlist">Playlist</a><br/>
<a href="/time">Time and Alarm</a><br/>
<a href="/wifi">Wi-Fi settings</a><br/>
<a href="/update">Update Firmware</a>
<script>
  function updateTrack() {
    fetch('/currentTrack')
      .then(response => response.text())
      .then(data => {
        document.getElementById('currentTrack').textContent = data;
      });
  }
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
  updateTrack();
  setInterval(updateTrack, 2000);
</script>
</body>
</html>
)rawliteral";

void configMainPage() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String main_page = FPSTR(main_page_start);   // start the HTML
    main_page += R"rawliteral(<label id="volumeLabel1">Volume:)rawliteral" + String(currentVolume) + "</label> <br/> ";
    main_page += R"rawliteral(<input type="range" min="0" max="21" value=")rawliteral" + String(currentVolume) + R"rawliteral(" id="volumeSlider1">)rawliteral"; 
    main_page += "<p> Alarm is set: " + String(alarmHour) + ":" + fineStrMinute(alarmMinute) + "</p>";
    main_page += FPSTR(main_page_end);           // finish the HTML
    request->send(200, "text/html", main_page);
  });
  server.on("/currentTrack", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", tracks[currentTrack]);
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
          EEPROM.write(VOLUME_ADDR, currentVolume);
          EEPROM.commit();
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
  <a href='/' style='margin-bottom: 20px; display: block;'>Home page</a>
  <b>Current Time: <span id="currentTime"></span></b><br/>
  <b>Alarm Time: <span id="alarmTime"></span></b><br/>
  <b>Set Alarm</b>
  <form action="/setAlarm">
    <input type="text" name="alarmHour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="alarmMinute" placeholder="Minute" style="width: 40px;">
    <input type="submit" value="Set Alarm">
  </form>

  <b>Set Time</b>
  <form action="/setTime">
    <input type="text" name="hour" placeholder="Hour" style="width: 40px;">
    <input type="text" name="minute" placeholder="Minute" style="width: 40px;">
    <input type="text" name="second" placeholder="Second" style="width: 50px;">
    <input type="submit" value="Set Time">
  </form> 

  <script>
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
      if(p->name()=="alarmHour") {
        alarmHour = p->value().toInt();
        EEPROM.write(ALARM_HOUR_ADDR, alarmHour);
      }
      if(p->name()=="alarmMinute") {
        alarmMinute = p->value().toInt();
        EEPROM.write(ALARM_MIN_ADDR, alarmMinute);
      }
    }
    flagModeAlarm = true;
    EEPROM.write(ALARM_FLAG_ADDR, flagModeAlarm);
    EEPROM.commit();
    request->send(200, "text/plain", "OK");
  });
  server.on("/getTime", HTTP_GET, [] (AsyncWebServerRequest *request) {
  DateTime now = rtc.now();
  String currentTime = String(now.hour()) + ":" + fineStrMinute(now.minute());
  request->send(200, "text/plain", currentTime);
  });
  server.on("/getAlarm", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String alarmTime = String(alarmHour) + ":" + fineStrMinute(alarmMinute);
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

void deleteTrack(String tracksList[], int &trackCount, const String &trackName) {
    int i = 0;
    while (i < trackCount) {
        if (tracksList[i] == trackName) {
            for (int j = i; j < trackCount - 1; j++) {
                tracksList[j] = tracksList[j + 1];
            }
            trackCount--;
        } else {
            i++;
        }
    }
}

String fineStrMinute(int minute){
  String fineMinute; 
  if (minute < 10){
    fineMinute += "0"; 
  }
  fineMinute += String(minute); 
  return fineMinute;
}