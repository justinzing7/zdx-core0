// ZDX CORE-0 — optional bulk storage slot (SD card, SPI flash FS, LittleFS).
//
// Interface only: the foundation fixes the SHAPE so audio sources, recorders
// and loggers are portable; the project picks the backend (SdFat, LittleFS,
// ESP32 FS). Not for settings — those use settings.h on NVM.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

enum class FileMode : uint8_t { Read, Write /* truncate */, Append };

class File {
 public:
  virtual ~File() {}
  virtual size_t   read(void* dst, size_t n) = 0;
  virtual size_t   write(const void* src, size_t n) = 0;
  virtual Status   seek(uint32_t pos) = 0;
  virtual uint32_t position() = 0;
  virtual uint32_t size() = 0;
  virtual Status   flush() = 0;
  virtual void     close() = 0;
};

class Storage {
 public:
  virtual ~Storage() {}
  virtual Status   mount() = 0;
  virtual void     unmount() = 0;
  virtual bool     mounted() = 0;
  // Returns nullptr on failure. Backends use a small static File pool
  // (CORE0 rule: no heap after init) — always close() what you open.
  virtual File*    open(const char* path, FileMode mode) = 0;
  virtual Status   remove(const char* path) = 0;
  virtual Status   mkdir(const char* path) = 0;
  // Iterate a directory: cb(name, size, is_dir, ctx); return false from cb to stop.
  virtual Status   list(const char* dir, bool (*cb)(const char*, uint32_t, bool, void*), void* ctx) = 0;
  virtual uint64_t free_bytes() = 0;
  virtual uint64_t total_bytes() = 0;
};

// Event codes for EventType::Storage.
enum class StorageCode : uint8_t { Mounted, Unmounted, Inserted, Removed, Error, Full };

namespace storage {
Status   attach(Storage& s);
Storage* get();     // nullptr if not attached / not mounted
extern const Module kModule;   // mounts in init, unmounts on Deep/Off, watches card-detect
}  // namespace storage

}  // namespace core0
