# ePaper Voice Recorder (ESP32-C6)

Firmware for a pocket voice recorder with an e-paper screen. It records audio to
a microSD card, transcribes recordings with the OpenAI API and serves a small web
portal on your local network for listening, reading and tagging notes.

Firmware version 1.2.0-rc9. The user interface and portal are in Danish.

## Hardware

Waveshare ESP32-C6-ePaper-1.54 with a microSD card formatted as FAT32.

- Display, power and battery-latch rails are switched by a TCA9554 I2C expander
  (address 0x20), so I2C starts before anything is powered.
- The microSD card runs over SPI on the display's bus (`src/storage/sd_card.*`);
  the display driver locks the bus for each transfer.
- There is no PSRAM, and the deep-sleep wake button is PWR on GPIO 2.

## Secrets

No credentials are included in this repository. To build with your own Wi-Fi
details and OpenAI API key, copy the template and fill it in:

```sh
cp recorder/secrets.example.h recorder/secrets.h
```

```c
#define WIFI_SSID  "your-network"
#define WIFI_PASS  "your-wifi-password"
#define OPENAI_KEY "sk-..."
```

- `recorder/secrets.h` is listed in `.gitignore`. Never commit it, and check
  `git status` before every commit.
- The values are compiled into `firmware.bin`. Do not share or publish builds
  made with a filled-in `secrets.h`.
- The file is optional. Without it the firmware builds with empty defaults, and
  the values are only used while no setup is saved on the device.
- If a key has ever been committed or shared by mistake, revoke it in the
  OpenAI dashboard and change your Wi-Fi password. Deleting the file afterwards
  does not remove it from git history.

## Build and upload

Open this folder in VS Code with the PlatformIO extension. The project pins
Arduino-ESP32 3.2.0, the PioArduino platform and its library dependencies. The
first build downloads these and requires an internet connection.

With the PlatformIO command-line tools installed, run from this folder:

```sh
pio run -e esp32c6_epaper
pio run -e esp32c6_epaper -t upload
pio device monitor -b 115200
```

Connect the board by USB before uploading. If automatic bootloader entry fails,
hold BOOT while resetting the board, then release BOOT and retry the upload.
The build output is `.pio/build/esp32c6_epaper/firmware.bin`.

This is a PlatformIO project. Opening only the `.ino` in Arduino IDE does not
apply the pinned platform, partition layout or embedded certificate bundle.

## Use

Hold the upper button to start recording, then release it. Hold the upper
button again to stop and save. Recordings stop automatically after two hours.
Long recordings are saved in ten-minute parts. Use Sync from the menu to
transcribe pending recordings. Transfer in Settings opens the local portal on
the saved Wi-Fi network at `http://recorder.local` or the IP address shown on
the screen.

Recordings and transcripts are stored on the SD card under `/notes`. Back up
that directory before experimenting with storage formats. The portal has no
login, so only use it on a trusted private network.

## Make changes

- `recorder/recorder.ino`: startup, button actions and application flow.
- `recorder/config.h`: board pins, timing, idle-screen label and firmware defaults.
- `recorder/src/app/`: recording, storage, networking, settings and interface.
- `recorder/src/audio/`, `codec_board/`, `esp_codec_dev/`: audio hardware support.
- `recorder/src/display/`, `i2c_bsp/`, `power/`: display and board support.
- `platformio.ini`: pinned build settings, libraries and version identifier.
- `partitions_8MB.csv`: flash layout. The `setupcfg` partition can hold a
  one-time setup (Wi-Fi and API key) that is imported and erased on boot.
- `certs/ca_bundle.pem`: embedded root certificates for HTTPS connections.

Keep the partition layout and memory settings unless your hardware changes
require a migration. Version identifiers are set in `platformio.ini`, with
fallbacks in `recorder/config.h`. Normal uploads preserve device settings;
full-chip erases and partition changes may not.

The firmware uses OpenAI services for transcription. That needs Wi-Fi and your
own API key and may incur API usage charges. Audio recording works offline.
See `THIRD_PARTY.md` for bundled component notices.
