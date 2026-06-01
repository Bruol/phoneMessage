

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

const byte switchPins[] = {RESET, PhonePicked};
const byte switchCount = sizeof(switchPins) / sizeof(switchPins[0]);

byte activeInputs = 0;
bool switchClosed[switchCount] = {false, false};
bool lastSwitchReading[switchCount] = {HIGH, HIGH};
unsigned long lastSwitchChange[switchCount] = {0, 0};

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
