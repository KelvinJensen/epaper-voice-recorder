#pragma once
#include "FS.h"
#include "sdmmc_cmd.h"

// TF card over SPI. The C6 has no SDMMC host, and the card shares SPI2 with
// the e-paper panel, so the panel driver must initialize the bus first.
class SdSpiFS : public fs::FS {
public:
  explicit SdSpiFS(fs::FSImplPtr impl);
  bool begin(int spiHost, int csPin, const char* mountpoint = "/sdcard", uint8_t maxOpenFiles = 5);
  void end();
  uint64_t totalBytes();
  uint64_t usedBytes();

private:
  sdmmc_card_t* _card = nullptr;
};

extern SdSpiFS SDCard;
