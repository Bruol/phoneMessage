# Phone Message

An ESP32-based phone-message player. A 4x3 keypad acts as the trigger surface: pressing any key starts playback of a WAV file from a microSD card, and releasing the final active key stops playback. Audio is sent over I2S to an external DAC or amplifier.

## Demo

<video src="public/IMG_5139_compressed.mp4" controls width="420"></video>

## Build Photos

![Phone message build front](public/A9B305BA-74C4-4937-9626-EFCFB8D2E1C4_1_105_c.jpeg)

![Phone message build wiring](public/07ED63B4-7AB4-48EC-8872-88601F5C9FC7_1_102_o.jpeg)

## Hardware

- ESP32 development board
- 4x3 matrix keypad
- microSD card module
- I2S audio DAC or amplifier
- Speaker
- microSD card formatted for the ESP32 SD library

## Firmware

This project uses PlatformIO with the Arduino framework.

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
```

Libraries:

- `esphome/ESP32-audioI2S`
- `chris--a/Keypad`

## Pinout

### microSD Card

| Signal | ESP32 GPIO |
| --- | --- |
| CS | 5 |
| MOSI | 23 |
| MISO | 19 |
| SCK | 18 |

### I2S Audio

| Signal | ESP32 GPIO |
| --- | --- |
| DIN / DOUT | 22 |
| BCLK | 26 |
| LRC / WS | 25 |

### Keypad

| Keypad line | ESP32 GPIO |
| --- | --- |
| Row 1 | 32 |
| Row 2 | 33 |
| Row 3 | 13 |
| Row 4 | 14 |
| Column 1 | 27 |
| Column 2 | 16 |
| Column 3 | 17 |

## Audio Files

Copy WAV files to the microSD card with a JSON metadata file next to each WAV file. The metadata file must use the same base name as the audio file:

```text
/Lorin_Urbantat.wav
/Lorin_Urbantat.json
```

The JSON metadata must contain a `number` field with the 2- or 3-digit keypad number for that voice node:

```json
{
  "number": "12"
}
```

Numeric values also work:

```json
{
  "number": 123
}
```

At startup, the firmware scans the SD card for `.wav` files, looks for matching `.json` files, parses each `number`, and maps that keypad number to the WAV file. Enter the mapped 2- or 3-digit number on the keypad; after a short pause with no new digit pressed, the firmware confirms the entered number and starts the matching voice node. The reset switch clears the entered number and stops playback.

The `Voice Notes/` folder contains source voice-note assets and converted WAV files that can be copied to the microSD card.

## Usage

1. Wire the ESP32, keypad, microSD module, and I2S audio output using the pin tables above.
2. Place the target WAV file at the root of the microSD card.
3. Install PlatformIO.
4. Build and upload:

```sh
pio run --target upload
```

5. Open the serial monitor:

```sh
pio device monitor
```

When a mapped keypad number is entered and the input timeout expires, the ESP32 starts the matching WAV file. Playback continues after the keys are released. Press the reset switch to clear input and stop playback.

## Project Layout

```text
.
|-- src/main.cpp          # ESP32 firmware
|-- platformio.ini        # PlatformIO configuration
|-- ESP32_PINOUT.md       # ESP32 dev board pin reference
|-- Voice Notes/          # Voice-note source and WAV assets
`-- public/               # README images and demo video
```
