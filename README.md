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

Copy MP3 files into the `/long` directory on the microSD card with a JSON metadata file next to each MP3 file. The metadata file must use the same base name as the audio file:

```text
/long/Lorin_Urbantat.mp3
/long/Lorin_Urbantat.json
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

At startup, the firmware scans `/long` for direct-dial voice notes and `/short` for categorized short voice notes. Hidden files whose names start with `.` are ignored.

Long voice notes use matching `.json` files with a `number` field. Enter the mapped 2- or 3-digit number on the keypad; after a short pause with no new digit pressed, the firmware confirms the entered number and starts the matching voice node.

Short voice notes use matching `.metadata.json` files with `tags` such as `[1, 4]`, or a `category` field. Keypad digits `1` through `5` select the matching short-note category and play a random note from that category. A short note can belong to multiple categories.

Short category names are logged over serial when selected: `1` is `wanna feel beautiful?`, `2` is `wanna feel loved?`, `3` is `wanna feel smart?`, `4` is `wanna feel seen?`, and `5` is `wanna laugh?`.

The reset switch clears the entered number and stops playback.

The `voice_notes/` folder contains source voice-note assets and converted MP3 files that can be copied to the microSD card.

Keypad digit tones are loaded from `/keys/<digit>.wav` on the microSD card, for example `/keys/1.wav`. Keypad digits are still mapped to 1-indexed tone files, so key `0` plays `/keys/1.wav`, key `1` plays `/keys/2.wav`, and key `9` plays `/keys/10.wav`.

If the entered keypad number is not assigned to a voice message, the firmware plays `/dial_out_beep.mp3`.

## Usage

1. Wire the ESP32, keypad, microSD module, and I2S audio output using the pin tables above.
2. Place direct-dial MP3 files and matching JSON metadata files under `/long` on the microSD card. Place categorized short MP3 files and matching `.metadata.json` files under `/short`.
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

## Volume Control

Double press the reset input on GPIO 21, then press a keypad number to set the volume. Key `0` sets volume level 1, key `1` sets volume level 2, and so on up to key `9`, which sets volume level 10.

## SD Card Upload Mode

On startup the ESP32 leaves Wi-Fi disconnected and runs the normal phone-message player. To edit the SD card over Wi-Fi, press and hold the reset input on GPIO 21 for 5 seconds.

Upload mode stops playback and exposes the SD card through the `ESPWebFileManager` web interface. The page lists SD-card contents and supports file and folder operations such as upload, download, create, and delete.

### Existing Wi-Fi Network

Upload mode only works on an existing Wi-Fi network. Build the `esp32dev_wifi` environment with these environment variables set:

```sh
export PHONE_MESSAGE_WIFI_SSID='Your Wi-Fi Name'
export PHONE_MESSAGE_WIFI_PASSWORD='Your Wi-Fi Password'
pio run -e esp32dev_wifi --target upload
```

Then hold the reset input for 5 seconds. If the ESP32 connects successfully, the serial monitor prints the file manager address:

```text
SD-card Wi-Fi update mode is ready: http://192.168.1.42/file
```

If credentials are missing or the network cannot be reached within 15 seconds, upload mode is not started and the phone-message player remains in normal mode.

Press the reset input again while upload mode is running to stop the web file manager, disconnect Wi-Fi, rescan SD-card metadata, and return to normal phone-message playback mode.

## Project Layout

```text
.
|-- src/main.cpp          # ESP32 firmware
|-- platformio.ini        # PlatformIO configuration
|-- ESP32_PINOUT.md       # ESP32 dev board pin reference
|-- Voice Notes/          # Voice-note source and WAV assets
`-- public/               # README images and demo video
```
