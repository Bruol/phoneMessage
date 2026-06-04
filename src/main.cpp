

// Include required libraries
#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "Keypad.h"
#include "WiFi.h"
#include "WebServer.h"
#include "DNSServer.h"
#include "ArduinoOTA.h"

// microSD Card Reader connections
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18
#define SD_SPI_FREQ 10000000

// I2S Connections
#define I2S_DOUT 22
#define I2S_BCLK 26
#define I2S_LRC 25

#define RESET 21
#define PhonePicked 4
#define SWITCH_DEBOUNCE_MS 50
#define UPLOAD_MODE_HOLD_MS 5000
#define RESET_DOUBLE_PRESS_MS 700

const char *UPLOAD_AP_SSID = "PhoneMessage Upload";
const char *OTA_HOSTNAME = "phone-message";
const byte DNS_PORT = 53;
const byte DEFAULT_VOLUME_LEVEL = 5;

#define AUDIO_FILE "/Lorin_Urbantat.wav"

const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},

    {'7', '8', '9'},
    {'*', '0', '#'}};

byte rowPins[ROWS] = {32, 33, 13, 14};
byte colPins[COLS] = {27, 16, 17};

// Create Audio object
Audio audio;
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
WebServer server(80);
DNSServer dnsServer;

const byte switchPins[] = {RESET, PhonePicked};
const byte switchCount = sizeof(switchPins) / sizeof(switchPins[0]);

byte activeInputs = 0;
bool switchClosed[switchCount] = {false, false};
bool lastSwitchReading[switchCount] = {HIGH, HIGH};
unsigned long lastSwitchChange[switchCount] = {0, 0};
unsigned long resetHoldStart = 0;
unsigned long lastResetPressRelease = 0;
bool resetPressed = false;
bool resetHoldHandled = false;
bool uploadMode = false;
bool waitingForVolumeKey = false;
byte volumeLevel = DEFAULT_VOLUME_LEVEL;

String htmlEscape(const String &value)
{
  String escaped = value;
  escaped.replace("&", "&amp;");
  escaped.replace("<", "&lt;");
  escaped.replace(">", "&gt;");
  escaped.replace("\"", "&quot;");
  escaped.replace("'", "&#39;");
  return escaped;
}

String urlEncode(const String &value)
{
  String encoded;
  const char *hex = "0123456789ABCDEF";

  for (size_t i = 0; i < value.length(); i++)
  {
    char c = value[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/')
    {
      encoded += c;
    }
    else
    {
      encoded += '%';
      encoded += hex[(c >> 4) & 0x0F];
      encoded += hex[c & 0x0F];
    }
  }

  return encoded;
}

String normalizePath(String path)
{
  path.trim();
  path.replace("\\", "/");

  if (!path.startsWith("/"))
  {
    path = "/" + path;
  }

  while (path.indexOf("//") >= 0)
  {
    path.replace("//", "/");
  }

  if (path.indexOf("..") >= 0)
  {
    return "/";
  }

  return path;
}

void appendFileRows(String &page, fs::File dir)
{
  File file = dir.openNextFile();
  while (file)
  {
    String path = file.path();
    bool directory = file.isDirectory();

    page += "<tr><td>";
    page += directory ? "dir" : "file";
    page += "</td><td><a href=\"/file?path=";
    page += urlEncode(path);
    page += "\">";
    page += htmlEscape(path);
    page += "</a></td><td>";
    page += directory ? "-" : String(file.size());
    page += "</td><td>";

    if (!directory)
    {
      page += "<form method=\"POST\" action=\"/delete\" onsubmit=\"return confirm('Delete ";
      page += htmlEscape(path);
      page += "?')\"><input type=\"hidden\" name=\"path\" value=\"";
      page += htmlEscape(path);
      page += "\"><button type=\"submit\">Delete</button></form>";
    }

    page += "</td></tr>";

    if (directory)
    {
      appendFileRows(page, file);
    }

    file = dir.openNextFile();
  }
}

void sendIndex()
{
  String page = F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                  "<title>Phone Message Upload</title><style>"
                  "body{font-family:system-ui,Arial,sans-serif;margin:24px;max-width:900px}"
                  "table{width:100%;border-collapse:collapse;margin:20px 0}td,th{border-bottom:1px solid #ddd;padding:8px;text-align:left}"
                  "form{margin:0}button,input{font:inherit}button{padding:6px 10px}"
                  ".upload{padding:16px;border:1px solid #ddd;border-radius:8px}"
                  "</style></head><body><h1>Phone Message SD Card</h1>"
                  "<div class=\"upload\"><form method=\"POST\" action=\"/upload\" enctype=\"multipart/form-data\">"
                  "<p><input type=\"file\" name=\"file\" required> <button type=\"submit\">Upload</button></p>"
                  "</form></div><table><thead><tr><th>Type</th><th>Path</th><th>Bytes</th><th></th></tr></thead><tbody>");

  File root = SD.open("/");
  appendFileRows(page, root);
  page += F("</tbody></table></body></html>");
  server.send(200, "text/html", page);
}

void handleUpload()
{
  static File uploadFile;
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START)
  {
    String filename = normalizePath(upload.filename);
    if (SD.exists(filename))
    {
      SD.remove(filename);
    }
    uploadFile = SD.open(filename, FILE_WRITE);
  }
  else if (upload.status == UPLOAD_FILE_WRITE)
  {
    if (uploadFile)
    {
      uploadFile.write(upload.buf, upload.currentSize);
    }
  }
  else if (upload.status == UPLOAD_FILE_END)
  {
    if (uploadFile)
    {
      uploadFile.close();
    }
  }
}

void startUploadMode()
{
  if (uploadMode)
  {
    return;
  }

  uploadMode = true;
  activeInputs = 0;
  audio.stopSong();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(UPLOAD_AP_SSID);

  IPAddress apIp = WiFi.softAPIP();
  dnsServer.start(DNS_PORT, "*", apIp);

  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA
      .onStart([]()
               {
                 Serial.println("OTA firmware update started");
                 audio.stopSong();
               })
      .onEnd([]()
             {
               Serial.println("\nOTA firmware update finished");
             })
      .onError([](ota_error_t error)
               {
                 Serial.printf("OTA error %u\n", error);
               });
  ArduinoOTA.begin();

  server.on("/", HTTP_GET, sendIndex);
  server.on("/generate_204", HTTP_GET, sendIndex);
  server.on("/fwlink", HTTP_GET, sendIndex);
  server.on("/hotspot-detect.html", HTTP_GET, sendIndex);
  server.on("/upload", HTTP_POST, []()
            {
              server.sendHeader("Location", "/");
              server.send(303);
            },
            handleUpload);
  server.on("/delete", HTTP_POST, []()
            {
              String path = normalizePath(server.arg("path"));
              if (path != "/" && SD.exists(path))
              {
                SD.remove(path);
              }
              server.sendHeader("Location", "/");
              server.send(303);
            });
  server.on("/file", HTTP_GET, []()
            {
              String path = normalizePath(server.arg("path"));
              if (!SD.exists(path))
              {
                server.send(404, "text/plain", "File not found");
                return;
              }

              File file = SD.open(path, FILE_READ);
              server.streamFile(file, "application/octet-stream");
              file.close();
            });
  server.onNotFound(sendIndex);
  server.begin();

  Serial.printf("Upload mode started. Connect to Wi-Fi \"%s\" and open http://%s/\n",
                UPLOAD_AP_SSID,
                apIp.toString().c_str());
  Serial.printf("PlatformIO OTA target: %s at %s\n", OTA_HOSTNAME, apIp.toString().c_str());
}

void setVolumeLevel(byte level)
{
  if (level < 1 || level > 10)
  {
    return;
  }

  volumeLevel = level;
  audio.setVolume(volumeLevel);
  Serial.printf("Volume set to %u\n", volumeLevel);
}

void inputClosed(const char *inputName)
{
  activeInputs++;
  Serial.printf("%s closed: starting audio\n", inputName);
  audio.stopSong();
  audio.connecttoFS(SD, AUDIO_FILE);
}

void inputOpened(const char *inputName)
{
  if (activeInputs > 0)
  {
    activeInputs--;
  }

  Serial.printf("%s open\n", inputName);

  if (activeInputs == 0)
  {
    Serial.println("All inputs open: stopping audio");
    audio.stopSong();
  }
}

void readSwitches()
{
  for (byte i = 0; i < switchCount; i++)
  {
    if (switchPins[i] == RESET && resetHoldStart > 0)
    {
      continue;
    }

    bool reading = digitalRead(switchPins[i]);

    if (reading != lastSwitchReading[i])
    {
      lastSwitchReading[i] = reading;
      lastSwitchChange[i] = millis();
    }

    if (millis() - lastSwitchChange[i] < SWITCH_DEBOUNCE_MS)
    {
      continue;
    }

    bool closed = reading == LOW;
    if (closed == switchClosed[i])
    {
      continue;
    }

    switchClosed[i] = closed;

    char switchName[10];
    snprintf(switchName, sizeof(switchName), "Switch %u", i + 1);

    if (closed)
    {
      inputClosed(switchName);
    }
    else
    {
      inputOpened(switchName);
    }
  }
}

void checkUploadModeRequest()
{
  if (uploadMode)
  {
    return;
  }

  bool pressed = digitalRead(RESET) == LOW;

  if (pressed)
  {
    if (!resetPressed)
    {
      resetPressed = true;
      resetHoldHandled = false;
      resetHoldStart = millis();
    }
    else if (!resetHoldHandled && millis() - resetHoldStart >= UPLOAD_MODE_HOLD_MS)
    {
      resetHoldHandled = true;
      startUploadMode();
    }
  }
  else
  {
    if (resetPressed && !resetHoldHandled)
    {
      unsigned long now = millis();
      if (lastResetPressRelease > 0 && now - lastResetPressRelease <= RESET_DOUBLE_PRESS_MS)
      {
        waitingForVolumeKey = true;
        lastResetPressRelease = 0;
        Serial.println("Volume select armed. Press keypad 0-9.");
      }
      else
      {
        lastResetPressRelease = now;
      }
    }

    resetPressed = false;
    resetHoldHandled = false;
    resetHoldStart = 0;
  }
}

void setup()
{
  // Start Serial Port
  Serial.begin(115200);
  delay(1000);

  // Set microSD Card CS as OUTPUT and set HIGH
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(RESET, INPUT_PULLUP);
  pinMode(PhonePicked, INPUT_PULLUP);

  // Initialize SPI bus for microSD Card
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  delay(100);

  // Start microSD Card
  if (!SD.begin(SD_CS, SPI, SD_SPI_FREQ))
  {
    Serial.println("Error accessing microSD card!");
    while (true)
      ;
  }

  Serial.printf("microSD card mounted. Type: %u, Size: %llu MB\n",
                SD.cardType(),
                SD.cardSize() / (1024ULL * 1024ULL));

  // Setup I2S
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

  // Set Volume
  setVolumeLevel(DEFAULT_VOLUME_LEVEL);

  keypad.setDebounceTime(50);
  keypad.setHoldTime(500);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
}

void loop()
{
  checkUploadModeRequest();

  if (uploadMode)
  {
    dnsServer.processNextRequest();
    server.handleClient();
    ArduinoOTA.handle();
    return;
  }

  if (keypad.getKeys())
  {
    for (byte i = 0; i < LIST_MAX; i++)
    {
      if (!keypad.key[i].kchar)
      {
        continue;
      }

      switch (keypad.key[i].kstate)
      {
      case PRESSED:
      {
        if (waitingForVolumeKey)
        {
          char key = keypad.key[i].kchar;
          if (key >= '0' && key <= '9')
          {
            setVolumeLevel((key - '0') + 1);
            waitingForVolumeKey = false;
          }
          break;
        }

        char keyName[7];
        snprintf(keyName, sizeof(keyName), "Key %c", keypad.key[i].kchar);
        inputClosed(keyName);
        break;
      }

      case RELEASED:
      {
        char keyName[7];
        snprintf(keyName, sizeof(keyName), "Key %c", keypad.key[i].kchar);
        inputOpened(keyName);
        break;
      }

      default:
        break;
      }
    }
  }

  readSwitches();

  if (activeInputs > 0)
  {
    audio.loop();
  }
}
