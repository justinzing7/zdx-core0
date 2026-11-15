# Settings

Header: `settings.h` · Event type: `EventType::Settings` · Flag: `CORE0_FEAT_SETTINGS`

## Model

- The app defines **one POD struct** with fixed-size fields (`int16_t` / `uint8_t` / char arrays) and no pointers, plus a `Version`. The typed wrapper `Settings<T, Version>` binds it to the service.
- **Storage layout** in the NVM region (`hal::nvm_*`): two slots, A and B. Each slot holds `SettingsHeader{magic 'ZC', version, size, crc16, seq}` followed by the payload.
- **Load** (in `init`):
  1. Read both headers.
  2. A slot is valid when magic matches, size matches, and the CRC is correct.
  3. Pick the valid slot with the higher `seq`.
  4. If the version matches, use it and post `Loaded`.
  5. If the version differs and `migrate` exists, apply defaults first, then call `migrate(dst, old, old_size, old_version)`. Success posts `Migrated`; failure keeps defaults.
  6. If no slot is valid, apply defaults and post `Defaulted`.
- **Save:** write the **other** slot with `seq + 1`, then `nvm_commit()`. A power cut mid-write leaves the previous slot intact.
- **Deferred commit:** `mark_dirty()` (or `settings.changed()`) restarts a `commit_delay_ms` timer, default 3 s. The save happens when the timer fires, on `on_suspend`, or on `save_now()`.

## Rules

- **Version changes:**
  - Adding a field at the end means bumping the version. The migration copies the old prefix and keeps defaults for the new fields; that is the default generated migrate.
  - Removing or reordering fields also means bumping the version and writing an explicit migrate.
- **Ranges:** values are validated on load by the app's `apply_defaults`/validate step. A corrupt-but-CRC-valid value must never crash the UI, so clamp ranges.
- **What doesn't go here:** settings hold user preferences and small device state only. Large data goes to storage.
- **Secrets** such as Wi-Fi credentials: use the product's secure store if one exists. Otherwise they may live in the settings struct, placed at its end, with `Binding::redact_from` (or the `Settings<>` constructor argument) set to their offset. `set dump` then prints those bytes as `**`, and the header line says `redacted from N`. This hides them from the console only: unencrypted NVM can still be read with a flash reader.
- **Factory reset** (`factory_reset()`): reachable from the settings menu, with confirmation, and from the CLI.

## Acceptance

- [ ] Power is cut (simulated with the mock NVM) at every byte offset during a save, and the next load always returns either the old or the new settings, never defaults or garbage.
- [ ] 10 rapid `mark_dirty()` calls result in 1 write.
- [ ] Bumping the version with an appended field preserves old values and defaults the new one.
- [ ] Settings survive reset, Sleep, Deep, and firmware update (same version).
- [x] `set dump` never prints bytes at or after `redact_from` (native test, 0.2.0).
