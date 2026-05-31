

// Include required libraries
#include "Arduino.h"
#include "Audio.h"
#include "SD.h"
#include "FS.h"

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

#define SWITCH_PIN 27
#define AUDIO_FILE "/Lorin_Urbantat.wav"
#define SWITCH_DEBOUNCE_MS 50

// Create Audio object
Audio audio;
bool lastRawSwitchClosed = false;
bool lastSwitchClosed = false;
unsigned long lastSwitchChangeMs = 0;

void setup()
{
  // Start Serial Port
  Serial.begin(115200);
  delay(1000);

  pinMode(SWITCH_PIN, INPUT_PULLDOWN);

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
}

void loop()
{
  bool rawSwitchClosed = digitalRead(SWITCH_PIN) == HIGH;

  if (rawSwitchClosed != lastRawSwitchClosed)
  {
    lastRawSwitchClosed = rawSwitchClosed;
    lastSwitchChangeMs = millis();
  }

  if ((millis() - lastSwitchChangeMs) >= SWITCH_DEBOUNCE_MS &&
      rawSwitchClosed != lastSwitchClosed)
  {
    lastSwitchClosed = rawSwitchClosed;

    if (lastSwitchClosed)
    {
      Serial.println("Switch closed: starting audio");
      audio.stopSong();
      audio.connecttoFS(SD, AUDIO_FILE);
    }
    else
    {
      Serial.println("Switch open: stopping audio");
      audio.stopSong();
    }
  }

  if (lastSwitchClosed)
  {
    audio.loop();
  }
}
