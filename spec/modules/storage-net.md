# Optional slots: storage and connectivity

Headers: `storage.h`, `net.h` · Flags: `CORE0_FEAT_STORAGE`, `CORE0_FEAT_NET` (both off by default)

These are optional slots. The foundation fixes their shape so features such as players, recorders, loggers, and weather fetchers are portable, while each product chooses the backend. Storage is interface-only; since 0.2.0 the net service is implemented (`net.cpp`) and only the radio driver is the product's.

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

### Net service rules (0.2.0)

- Up to `CORE0_NET_MAX_USERS` holders (default 2). `acquire()` returns -1 when all slots are taken. `who` must outlive the hold.
- The first `acquire()` calls `connect()`. The last `release()` calls `disconnect()`. `release()` sets the caller's id to -1, and releasing twice is a no-op.
- `tick` polls `NetDriver::state()`, which must be cheap (a cached value), and posts `StateChanged` with `data = kAllUsers` on every change.
- A newcomer while the radio is Up is sent `StateChanged(Up)` with its own id.
- A holder whose `timeout_ms` passes before Up is sent `StateChanged(Error)` with its id, once. It keeps its slot: the holder decides when to release.
- `can_enter` vetoes Sleep and below while anyone holds the radio, or until the driver reports Off after the last release. A driver must reach Off after `disconnect()`, or the device never sleeps.
- `acquire()` while the radio is in Error, with other holders, does not reconnect. The newcomer times out. The radio is retried after every holder has released.
- CLI `net`: state, holders, RSSI while Up, and `NetDriver::describe()` text.

## Acceptance (when enabled)

- [ ] Removing the SD card mid-write posts `Removed`, the writer reports an error, and the device keeps running.
- [x] Reference counting, timeouts, the Sleep veto and the CLI view: `test_core0_net` (0.2.0).
- [ ] With no `acquire()` callers, radio current is at its off level (measure it).
- [ ] An acquire → Up → release cycle completes and the radio powers down within 1 s.
