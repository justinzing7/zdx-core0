# Optional slots: storage and connectivity

Headers: `storage.h`, `net.h` · Flags: `CORE0_FEAT_STORAGE`, `CORE0_FEAT_NET` (both off by default)

These are **interface-only** slots. The foundation fixes their shape so features such as players, recorders, loggers, and weather fetchers are portable, while each product chooses the backend.

## Storage

- Backends: SdFat (SD over SPI/SDMMC), LittleFS on SPI flash or internal flash, or the ESP32 FS.
- Each backend implements `Storage` and `File` with a **static pool** of File objects (typically 2–4). `open()` returns nullptr when the pool is exhausted.
- `storage::kModule`:
  - Mounts in `init` (non-blocking retry in tick if no card is present).
  - Watches card-detect if wired, posting `Inserted`/`Removed`.
  - Flushes and unmounts in `on_suspend` for Deep/Off.
  - Vetoes Sleep while a File opened for writing is open. Writers should hold a PowerLock explicitly.
- Write-heavy features (the recorder) buffer in RAM and write in large aligned blocks (≥ 4 KB for SD), because SD write latency spikes reach 100+ ms.
- Free space is checked before starting a long write; `Full` is posted when the free-space threshold is crossed.

## Connectivity

- The radio is **off unless someone holds it**. `acquire(who, timeout)` starts the connection; the caller waits for `NetCode::StateChanged(Up)` or `Error`/timeout, then calls `release()`. With zero users, the driver disconnects and powers the radio down.
- Typical flows:
  - NTP sync on boot plus once a day (clock source Ntp).
  - Weather fetch every N minutes, waking from Sleep via RTC.
  - OTA only when on charger.
- The driver owns credentials and provisioning. A captive portal or BLE provisioning screen belongs to the product, but should be built as an ordinary screen.
- `net::kModule` vetoes Sleep while a user holds the radio.

## Acceptance (when enabled)

- [ ] Removing the SD card mid-write posts `Removed`, the writer reports an error, and the device keeps running.
- [ ] With no `acquire()` callers, radio current is at its off level (measure it).
- [ ] An acquire → Up → release cycle completes and the radio powers down within 1 s.
