

// Include required libraries
#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include "ESPWebFileManager.h"
#include "Keypad.h"

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
#define WIFI_CONNECT_TIMEOUT_MS 15000

const byte DEFAULT_VOLUME_LEVEL = 5;
const char *DIAL_OUT_BEEP_PATH = "/dial_out_beep.mp3";
const byte SHORT_CATEGORY_MIN = 1;
const byte SHORT_CATEGORY_MAX = 5;
const char *SHORT_CATEGORY_NAMES[] = {
    "wanna feel beautiful?",
    "wanna feel loved?",
    "wanna feel smart?",
    "wanna feel seen?",
    "wanna laugh?"};

#ifndef WIFI_SSID
#define WIFI_SSID "co_werk_5"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "TAMAM_1312"
#endif

#define MAX_VOICE_NODES 64
#define MAX_SHORT_VOICE_NOTES 128
#define MAX_AUDIO_PATH_LENGTH 96
#define MAX_METADATA_BYTES 8192
#define MAX_SCAN_DEPTH 4
#define KEYPAD_CONFIRM_TIMEOUT_MS 1200
#define KEY_TONE_PATH_LENGTH 18


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
AsyncWebServer server(80);
ESPWebFileManager fileManager(FS_SD, false, true, SD_CS, SPI_MOSI, SPI_MISO, SPI_SCK);

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
bool uploadServerConfigured = false;
bool waitingForVolumeKey = false;
byte volumeLevel = DEFAULT_VOLUME_LEVEL;
bool keyToneActive = false;

bool isPhonePickedOpen()
{
  return digitalRead(PhonePicked) == HIGH;
}

void clearKeypadBuffer();
void stopPlayback();
void startUploadMode();
void stopUploadMode();
void scanLongVoiceNodes(const char *directoryPath);
void scanShortVoiceNotes(const char *directoryPath);
void scanVoiceNodeDirectories();
void playKeyTone(char digit);
void playDialOutBeep();

bool connectConfiguredWifi()
{
  if (strlen(WIFI_SSID) == 0)
  {
    Serial.println("Wi-Fi credentials are not configured; upload mode requires an existing Wi-Fi network");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to Wi-Fi network: %s\n", WIFI_SSID);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS)
  {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Wi-Fi connection failed; upload mode was not started");
    WiFi.disconnect(true);
    return false;
  }

  Serial.printf("Wi-Fi connected: http://%s/\n", WiFi.localIP().toString().c_str());
  return true;
}

void startUploadMode()
{
  if (uploadMode)
  {
    return;
  }

  stopPlayback();
  clearKeypadBuffer();
  audio.stopSong();

  if (!connectConfiguredWifi())
  {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    return;
  }

  uploadMode = true;
  if (!fileManager.begin())
  {
    Serial.println("ESPWebFileManager could not initialize the SD card");
    uploadMode = false;
    return;
  }

  if (!uploadServerConfigured)
  {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->redirect("/file"); });
    fileManager.setServer(&server);
    uploadServerConfigured = true;
  }
  server.begin();

  Serial.printf("SD-card Wi-Fi update mode is ready: http://%s/file\n",
                WiFi.localIP().toString().c_str());
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

struct VoiceNode
{
  char number[4];
  char audioPath[MAX_AUDIO_PATH_LENGTH];
};

struct ShortVoiceNote
{
  byte categoryMask;
  char audioPath[MAX_AUDIO_PATH_LENGTH];
};

VoiceNode voiceNodes[MAX_VOICE_NODES];
ShortVoiceNote shortVoiceNotes[MAX_SHORT_VOICE_NOTES];
byte voiceNodeCount = 0;
byte shortVoiceNoteCount = 0;
char keypadBuffer[4] = "";
byte keypadBufferLength = 0;
unsigned long lastKeypadDigitMs = 0;
bool audioActive = false;

void stopUploadMode()
{
  if (!uploadMode)
  {
    return;
  }

  Serial.println("Stopping SD-card Wi-Fi update mode");
  server.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  uploadMode = false;
  waitingForVolumeKey = false;
  clearKeypadBuffer();

  voiceNodeCount = 0;
  shortVoiceNoteCount = 0;
  scanVoiceNodeDirectories();
  Serial.printf("Reloaded %u long voice nodes and %u short voice notes\n", voiceNodeCount, shortVoiceNoteCount);
}

bool endsWithIgnoreCase(const char *text, const char *suffix)
{
  size_t textLength = strlen(text);
  size_t suffixLength = strlen(suffix);

  if (suffixLength > textLength)
  {
    return false;
  }

  return strcasecmp(text + textLength - suffixLength, suffix) == 0;
}

bool isHiddenPath(const char *path)
{
  const char *name = strrchr(path, '/');
  name = name ? name + 1 : path;
  return name[0] == '.';
}

void stopPlayback()
{
  if (!audioActive && !keyToneActive)
  {
    return;
  }

  Serial.println("Stopping audio");
  audio.stopSong();
  audioActive = false;
  keyToneActive = false;
}

void stopKeyTone()
{
  if (!keyToneActive)
  {
    return;
  }

  audio.stopSong();
  keyToneActive = false;
}

void startPlayback(const char *audioPath)
{
  Serial.printf("Starting audio: %s\n", audioPath);
  audio.stopSong();
  audio.connecttoFS(SD, audioPath);
  audioActive = true;
}

void playKeyTone(char digit)
{
  byte toneNumber = (digit - '0') + 1;
  if (toneNumber < 1 || toneNumber > 10)
  {
    return;
  }

  char tonePath[KEY_TONE_PATH_LENGTH];
  snprintf(tonePath, sizeof(tonePath), "/keys/%u.wav", toneNumber);

  audio.stopSong();
  audioActive = false;
  keyToneActive = false;
  audio.connecttoFS(SD, tonePath);
  keyToneActive = true;
}

void playDialOutBeep()
{
  audio.stopSong();
  audioActive = false;
  keyToneActive = false;
  audio.connecttoFS(SD, DIAL_OUT_BEEP_PATH);
  keyToneActive = true;
}

void clearKeypadBuffer()
{
  keypadBuffer[0] = '\0';
  keypadBufferLength = 0;
  lastKeypadDigitMs = 0;
}

bool readMetadata(const char *metadataPath, char *buffer, size_t bufferSize)
{
  File metadata = SD.open(metadataPath, FILE_READ);
  if (!metadata)
  {
    Serial.printf("Metadata not found: %s\n", metadataPath);
    return false;
  }

  size_t bytesRead = metadata.readBytes(buffer, bufferSize - 1);
  metadata.close();
  buffer[bytesRead] = '\0';
  return true;
}

bool readJsonString(char **cursor, char *value, size_t valueSize)
{
  char *start = strchr(*cursor, '"');
  if (!start)
  {
    return false;
  }

  start++;
  char *out = value;
  size_t remaining = valueSize;
  while (*start && *start != '"')
  {
    if (*start == '\\' && start[1])
    {
      start++;
    }

    if (remaining > 1)
    {
      *out++ = *start;
      remaining--;
    }
    start++;
  }

  if (*start != '"')
  {
    return false;
  }

  *out = '\0';
  *cursor = start + 1;
  return true;
}

bool readJsonNumberValue(char **cursor, char *number, size_t numberSize)
{
  char *value = strchr(*cursor, ':');
  if (!value)
  {
    return false;
  }

  value++;
  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n' || *value == '"')
  {
    value++;
  }

  byte digitCount = 0;
  while (isdigit(*value) && digitCount < numberSize - 1)
  {
    number[digitCount++] = *value++;
  }
  number[digitCount] = '\0';

  if (digitCount < 1 || digitCount > 3)
  {
    return false;
  }

  *cursor = value;
  return true;
}

bool readJsonPathValue(char **cursor, char *path, size_t pathSize)
{
  char *value = strchr(*cursor, ':');
  if (!value)
  {
    return false;
  }
  value++;

  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
  {
    value++;
  }

  *cursor = value;
  return readJsonString(cursor, path, pathSize);
}

void addVoiceNodeMapping(const char *number, const char *audioPath)
{
  if (voiceNodeCount >= MAX_VOICE_NODES)
  {
    Serial.printf("Voice node limit reached; skipping %s\n", audioPath);
    return;
  }

  strlcpy(voiceNodes[voiceNodeCount].number, number, sizeof(voiceNodes[voiceNodeCount].number));
  strlcpy(voiceNodes[voiceNodeCount].audioPath, audioPath, sizeof(voiceNodes[voiceNodeCount].audioPath));

  Serial.printf("Mapped keypad number %s to %s\n", voiceNodes[voiceNodeCount].number, voiceNodes[voiceNodeCount].audioPath);
  voiceNodeCount++;
}

void addShortVoiceNoteMapping(byte category, const char *audioPath)
{
  if (shortVoiceNoteCount >= MAX_SHORT_VOICE_NOTES)
  {
    Serial.printf("Short voice note limit reached; skipping %s\n", audioPath);
    return;
  }

  if (category < SHORT_CATEGORY_MIN || category > SHORT_CATEGORY_MAX)
  {
    return;
  }

  shortVoiceNotes[shortVoiceNoteCount].categoryMask = 1 << (category - 1);
  strlcpy(shortVoiceNotes[shortVoiceNoteCount].audioPath, audioPath, sizeof(shortVoiceNotes[shortVoiceNoteCount].audioPath));

  Serial.printf("Mapped short note category %u to %s\n", category, shortVoiceNotes[shortVoiceNoteCount].audioPath);
  shortVoiceNoteCount++;
}

void scanLongVoiceNodes(const char *directoryPath)
{
  char metadataPath[MAX_AUDIO_PATH_LENGTH];
  snprintf(metadataPath, sizeof(metadataPath), "%s/metadata.json", directoryPath);

  char buffer[MAX_METADATA_BYTES + 1];
  if (!readMetadata(metadataPath, buffer, sizeof(buffer)))
  {
    return;
  }

  char *cursor = buffer;
  while ((cursor = strstr(cursor, "\"number\"")) != nullptr)
  {
    char number[4];
    if (!readJsonNumberValue(&cursor, number, sizeof(number)))
    {
      Serial.printf("Long metadata has invalid number near: %.24s\n", cursor);
      cursor++;
      continue;
    }

    char *pathField = strstr(cursor, "\"path\"");
    if (!pathField)
    {
      Serial.printf("Long metadata missing path for number %s\n", number);
      break;
    }

    cursor = pathField;
    char audioPath[MAX_AUDIO_PATH_LENGTH];
    if (!readJsonPathValue(&cursor, audioPath, sizeof(audioPath)))
    {
      Serial.printf("Long metadata has invalid path for number %s\n", number);
      continue;
    }

    addVoiceNodeMapping(number, audioPath);
  }
}

void scanShortVoiceNotes(const char *directoryPath)
{
  char metadataPath[MAX_AUDIO_PATH_LENGTH];
  snprintf(metadataPath, sizeof(metadataPath), "%s/metadata.json", directoryPath);

  char buffer[MAX_METADATA_BYTES + 1];
  if (!readMetadata(metadataPath, buffer, sizeof(buffer)))
  {
    return;
  }

  for (byte category = SHORT_CATEGORY_MIN; category <= SHORT_CATEGORY_MAX; category++)
  {
    char key[4];
    snprintf(key, sizeof(key), "\"%u\"", category);

    char *field = strstr(buffer, key);
    if (!field)
    {
      continue;
    }

    char *arrayStart = strchr(field, '[');
    char *arrayEnd = arrayStart ? strchr(arrayStart, ']') : nullptr;
    if (!arrayStart || !arrayEnd)
    {
      Serial.printf("Short metadata has invalid array for category %u\n", category);
      continue;
    }

    char *cursor = arrayStart + 1;
    while (cursor < arrayEnd)
    {
      char *pathStart = strchr(cursor, '"');
      if (!pathStart || pathStart >= arrayEnd)
      {
        break;
      }

      cursor = pathStart;
      char audioPath[MAX_AUDIO_PATH_LENGTH];
      if (!readJsonString(&cursor, audioPath, sizeof(audioPath)))
      {
        break;
      }

      addShortVoiceNoteMapping(category, audioPath);
    }
  }
}

void scanVoiceNodeDirectories()
{
  scanLongVoiceNodes("/long");
  scanShortVoiceNotes("/short");
}

const VoiceNode *findVoiceNode(const char *number)
{
  for (byte i = 0; i < voiceNodeCount; i++)
  {
    if (strcmp(voiceNodes[i].number, number) == 0)
    {
      return &voiceNodes[i];
    }
  }

  return nullptr;
}

bool playRandomShortVoiceNote(byte category)
{
  if (category < SHORT_CATEGORY_MIN || category > SHORT_CATEGORY_MAX)
  {
    return false;
  }

  const char *categoryName = SHORT_CATEGORY_NAMES[category - SHORT_CATEGORY_MIN];
  byte categoryBit = 1 << (category - 1);
  byte matchCount = 0;
  for (byte i = 0; i < shortVoiceNoteCount; i++)
  {
    if (shortVoiceNotes[i].categoryMask & categoryBit)
    {
      matchCount++;
    }
  }

  if (matchCount == 0)
  {
    return false;
  }

  byte selectedMatch = random(matchCount);
  for (byte i = 0; i < shortVoiceNoteCount; i++)
  {
    if (!(shortVoiceNotes[i].categoryMask & categoryBit))
    {
      continue;
    }

    if (selectedMatch == 0)
    {
      stopKeyTone();
      Serial.printf("Playing random short note from category %u (%s): %s\n",
                    category,
                    categoryName,
                    shortVoiceNotes[i].audioPath);
      startPlayback(shortVoiceNotes[i].audioPath);
      clearKeypadBuffer();
      return true;
    }
    selectedMatch--;
  }

  return false;
}

bool playBufferedNumber()
{
  if (keypadBufferLength == 1 &&
      keypadBuffer[0] >= '1' &&
      keypadBuffer[0] <= '5' &&
      playRandomShortVoiceNote(keypadBuffer[0] - '0'))
  {
    return true;
  }

  const VoiceNode *voiceNode = findVoiceNode(keypadBuffer);
  if (!voiceNode)
  {
    return false;
  }

  stopKeyTone();
  startPlayback(voiceNode->audioPath);
  clearKeypadBuffer();
  return true;
}

void handleKeypadDigit(char digit)
{
  if (keypadBufferLength >= sizeof(keypadBuffer) - 1)
  {
    clearKeypadBuffer();
  }

  keypadBuffer[keypadBufferLength++] = digit;
  keypadBuffer[keypadBufferLength] = '\0';
  lastKeypadDigitMs = millis();
  Serial.printf("Entered keypad number: %s\n", keypadBuffer);
}

void confirmKeypadBufferAfterTimeout()
{
  if (keypadBufferLength == 0 ||
      millis() - lastKeypadDigitMs < KEYPAD_CONFIRM_TIMEOUT_MS)
  {
    return;
  }

  if (keypadBufferLength < 1)
  {
    Serial.printf("Ignoring incomplete keypad number: %s\n", keypadBuffer);
    clearKeypadBuffer();
    return;
  }

  if (!playBufferedNumber())
  {
    Serial.printf("No voice node mapped to keypad number: %s\n", keypadBuffer);
    playDialOutBeep();
    clearKeypadBuffer();
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
      activeInputs++;
      Serial.printf("%s closed\n", switchName);

      if (switchPins[i] == RESET)
      {
        clearKeypadBuffer();
        stopPlayback();
      }
    }
    else
    {
      if (activeInputs > 0)
      {
        activeInputs--;
      }
      Serial.printf("%s open\n", switchName);

      if (switchPins[i] == PhonePicked)
      {
        clearKeypadBuffer();
        stopPlayback();
      }
    }
  }
}

void checkUploadModeRequest()
{
  bool pressed = digitalRead(RESET) == LOW;

  if (uploadMode)
  {
    if (pressed)
    {
      if (!resetPressed)
      {
        resetPressed = true;
        resetHoldHandled = true;
        stopUploadMode();
      }
    }
    else
    {
      resetPressed = false;
      resetHoldHandled = false;
      resetHoldStart = 0;
    }
    return;
  }

  if (pressed)
  {
    if (!resetPressed)
    {
      resetPressed = true;
      resetHoldHandled = false;
      resetHoldStart = millis();
      clearKeypadBuffer();
      stopPlayback();
    }
    else if (!resetHoldHandled && millis() - resetHoldStart >= UPLOAD_MODE_HOLD_MS)
    {
      resetHoldHandled = true;
      Serial.println("Starting SD-card Wi-Fi update mode");
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
  randomSeed((uint32_t)esp_random());

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
  audio.stopSong();

  scanVoiceNodeDirectories();
  audio.stopSong();
  Serial.printf("Loaded %u long voice nodes and %u short voice notes\n", voiceNodeCount, shortVoiceNoteCount);

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
    delay(5);
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
        char key = keypad.key[i].kchar;

        if (waitingForVolumeKey)
        {
          if (key >= '0' && key <= '9')
          {
            setVolumeLevel((key - '0') + 1);
            clearKeypadBuffer();
            waitingForVolumeKey = false;
          }
          break;
        }

        if (isPhonePickedOpen())
        {
          clearKeypadBuffer();
          break;
        }

        if (isdigit(key))
        {
          playKeyTone(key);
          handleKeypadDigit(key);
        }

        break;
      }

      case RELEASED:
        break;

      default:
        break;
      }
    }
  }

  readSwitches();
  confirmKeypadBufferAfterTimeout();

  if (audioActive || keyToneActive)
  {
    audio.loop();
  }
}

void audio_eof_mp3(const char *info)
{
  if (keyToneActive)
  {
    keyToneActive = false;
  }
  else if (audioActive)
  {
    audioActive = false;
  }
}
