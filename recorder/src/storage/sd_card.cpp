#include "sd_card.h"
#include "vfs_api.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "ff.h"

static uint64_t fatSectorSize(FATFS* fsinfo) {
#if FF_MAX_SS != FF_MIN_SS
  return fsinfo->ssize;
#else
  (void)fsinfo;
  return FF_MAX_SS;
#endif
}

SdSpiFS::SdSpiFS(fs::FSImplPtr impl) : FS(impl) {}

bool SdSpiFS::begin(int spiHost, int csPin, const char* mountpoint, uint8_t maxOpenFiles) {
  if (_card) return true;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = spiHost;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.host_id = (spi_host_device_t)spiHost;
  slot.gpio_cs = (gpio_num_t)csPin;
  esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = maxOpenFiles;
  mountConfig.allocation_unit_size = 0;
  if (esp_vfs_fat_sdspi_mount(mountpoint, &host, &slot, &mountConfig, &_card) != ESP_OK) {
    _card = nullptr;
    return false;
  }
  _impl->mountpoint(mountpoint);
  return true;
}

void SdSpiFS::end() {
  if (!_card) return;
  esp_vfs_fat_sdcard_unmount(_impl->mountpoint(), _card);
  _impl->mountpoint(NULL);
  _card = nullptr;
}

uint64_t SdSpiFS::totalBytes() {
  FATFS* fsinfo;
  DWORD freeClusters;
  if (!_card || f_getfree("0:", &freeClusters, &fsinfo) != 0) return 0;
  return (uint64_t)fsinfo->csize * (fsinfo->n_fatent - 2) * fatSectorSize(fsinfo);
}

uint64_t SdSpiFS::usedBytes() {
  FATFS* fsinfo;
  DWORD freeClusters;
  if (!_card || f_getfree("0:", &freeClusters, &fsinfo) != 0) return 0;
  return (uint64_t)fsinfo->csize * ((fsinfo->n_fatent - 2) - fsinfo->free_clst) * fatSectorSize(fsinfo);
}

SdSpiFS SDCard = SdSpiFS(fs::FSImplPtr(new VFSImpl()));
