

// Include required libraries
#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"
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

#define MAX_VOICE_NODES 64
#define MAX_AUDIO_PATH_LENGTH 96
#define MAX_METADATA_BYTES 512
#define MAX_SCAN_DEPTH 4
#define KEYPAD_CONFIRM_TIMEOUT_MS 1200

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

const byte switchPins[] = {RESET, PhonePicked};
const byte switchCount = sizeof(switchPins) / sizeof(switchPins[0]);

byte activeInputs = 0;
bool switchClosed[switchCount] = {false, false};
bool lastSwitchReading[switchCount] = {HIGH, HIGH};
unsigned long lastSwitchChange[switchCount] = {0, 0};

struct VoiceNode
{
  char number[4];
  char audioPath[MAX_AUDIO_PATH_LENGTH];
};

VoiceNode voiceNodes[MAX_VOICE_NODES];
byte voiceNodeCount = 0;
char keypadBuffer[4] = "";
byte keypadBufferLength = 0;
unsigned long lastKeypadDigitMs = 0;
bool audioActive = false;

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

void stopPlayback()
{
  if (!audioActive)
  {
    return;
  }

  Serial.println("Stopping audio");
  audio.stopSong();
  audioActive = false;
}

void startPlayback(const char *audioPath)
{
  Serial.printf("Starting audio: %s\n", audioPath);
  audio.stopSong();
  audio.connecttoFS(SD, audioPath);
  audioActive = true;
}

void clearKeypadBuffer()
{
  keypadBuffer[0] = '\0';
  keypadBufferLength = 0;
  lastKeypadDigitMs = 0;
}

bool parseNumberMetadata(const char *metadataPath, char *number, size_t numberSize)
{
  File metadata = SD.open(metadataPath, FILE_READ);
  if (!metadata)
  {
    Serial.printf("Metadata not found: %s\n", metadataPath);
    return false;
  }

  char buffer[MAX_METADATA_BYTES + 1];
  size_t bytesRead = metadata.readBytes(buffer, MAX_METADATA_BYTES);
  metadata.close();
  buffer[bytesRead] = '\0';

  char *key = strstr(buffer, "\"number\"");
  if (!key)
  {
    key = strstr(buffer, "number");
  }

  if (!key)
  {
    Serial.printf("Metadata missing number: %s\n", metadataPath);
    return false;
  }

  char *value = strchr(key, ':');
  if (!value)
  {
    Serial.printf("Metadata has invalid number field: %s\n", metadataPath);
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

  if (digitCount < 2 || digitCount > 3)
  {
    Serial.printf("Metadata number must be 2 or 3 digits: %s\n", metadataPath);
    return false;
  }

  return true;
}

void addVoiceNode(const char *audioPath)
{
  if (voiceNodeCount >= MAX_VOICE_NODES)
  {
    Serial.printf("Voice node limit reached; skipping %s\n", audioPath);
    return;
  }

  char metadataPath[MAX_AUDIO_PATH_LENGTH];
  strlcpy(metadataPath, audioPath, sizeof(metadataPath));

  char *extension = strrchr(metadataPath, '.');
  if (!extension)
  {
    return;
  }
  strlcpy(extension, ".json", sizeof(metadataPath) - (extension - metadataPath));

  char number[4];
  if (!parseNumberMetadata(metadataPath, number, sizeof(number)))
  {
    return;
  }

  strlcpy(voiceNodes[voiceNodeCount].number, number, sizeof(voiceNodes[voiceNodeCount].number));
  strlcpy(voiceNodes[voiceNodeCount].audioPath, audioPath, sizeof(voiceNodes[voiceNodeCount].audioPath));

  Serial.printf("Mapped keypad number %s to %s\n", voiceNodes[voiceNodeCount].number, voiceNodes[voiceNodeCount].audioPath);
  voiceNodeCount++;
}

void scanVoiceNodes(const char *directoryPath, byte depth)
{
  if (depth > MAX_SCAN_DEPTH)
  {
    return;
  }

  File directory = SD.open(directoryPath);
  if (!directory || !directory.isDirectory())
  {
    Serial.printf("Could not open directory: %s\n", directoryPath);
    return;
  }

  while (true)
  {
    File entry = directory.openNextFile();
    if (!entry)
    {
      break;
    }

    const char *entryPath = entry.path();
    if (entry.isDirectory())
    {
      scanVoiceNodes(entryPath, depth + 1);
    }
    else if (endsWithIgnoreCase(entryPath, ".wav"))
    {
      addVoiceNode(entryPath);
    }

    entry.close();
  }

  directory.close();
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

bool playBufferedNumber()
{
  const VoiceNode *voiceNode = findVoiceNode(keypadBuffer);
  if (!voiceNode)
  {
    return false;
  }

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

  if (keypadBufferLength < 2)
  {
    Serial.printf("Ignoring incomplete keypad number: %s\n", keypadBuffer);
    clearKeypadBuffer();
    return;
  }

  if (!playBufferedNumber())
  {
    Serial.printf("No voice node mapped to keypad number: %s\n", keypadBuffer);
    clearKeypadBuffer();
  }
}

void readSwitches()
{
  for (byte i = 0; i < switchCount; i++)
  {
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
    }
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

  scanVoiceNodes("/", 0);
  Serial.printf("Loaded %u voice node metadata entries\n", voiceNodeCount);

  // Setup I2S
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);

  // Set Volume
  audio.setVolume(6);

  keypad.setDebounceTime(50);
  keypad.setHoldTime(500);
}

void loop()
{
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

        if (isdigit(key))
        {
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

  if (audioActive)
  {
    audio.loop();
  }
}
