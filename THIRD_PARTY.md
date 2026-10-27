# Third-party components

Original copyright and SPDX notices in the source files are retained.

- `recorder/src/esp_codec_dev`: Espressif codec support. Files marked
  `SPDX-License-Identifier: Apache-2.0` remain under Apache License 2.0;
  the license text is included in `licenses/Apache-2.0.txt`. Local adaptations
  integrate codec lifecycle, power control and streaming with this firmware.
- Board/display/audio support targets Waveshare ESP32-S3-ePaper-1.54.
  Preserve original notices in driver files. No blanket relicensing of
  third-party components is implied.
- PlatformIO downloads PioArduino/Arduino-ESP32, Adafruit GFX 1.12.1 and
  ArduinoJson 7.4.2 during build. They are not bundled in this source archive;
  their upstream licenses and notices apply to those dependencies.

