

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

byte activeKeys = 0;

void setup()
{
  // Start Serial Port
  Serial.begin(115200);
  delay(1000);

  // Set microSD Card CS as OUTPUT and set HIGH
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

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
        activeKeys++;
        Serial.printf("Key %c closed: starting audio\n", keypad.key[i].kchar);
        audio.stopSong();
        audio.connecttoFS(SD, AUDIO_FILE);
        break;

      case RELEASED:
        if (activeKeys > 0)
        {
          activeKeys--;
        }

        Serial.printf("Key %c open\n", keypad.key[i].kchar);

        if (activeKeys == 0)
        {
          Serial.println("All keys open: stopping audio");
          audio.stopSong();
        }
        break;

      default:
        break;
      }
    }
  }

  if (activeKeys > 0)
  {
    audio.loop();
  }
}
