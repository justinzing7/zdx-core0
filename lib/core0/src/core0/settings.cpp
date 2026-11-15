// ZDX CORE-0 — persistent settings (settings.h, spec/modules/settings.md).
//
// Layout: the hal NVM region is split into two equal slots A/B. Each slot = SettingsHeader
// (12 B) + payload. Load picks the valid slot with the higher seq; save writes the OTHER slot
// (payload first, header last) then nvm_commit(), so a power cut mid-write leaves the previous
// slot intact.
//
// Commits happen ONLY from the commit timer or on_suspend (constitution v1.1.0 Principle I
// exception: a bounded flash stall). mark_dirty() restarts the timer at commit_delay_ms;
// save_now()/factory_reset() re-arm it at 0 ms, so the write happens on the next loop pass,
// never inside the caller (a screen or CLI handler).
#include "config.h"
#if CORE0_FEAT_SETTINGS  // feature off = zero code (constitution IX)

#include "settings.h"

#include <stdio.h>
#include <string.h>

#include "log.h"
#include "port.h"
#include "timer.h"
#if CORE0_FEAT_CLI
#  include "cli.h"
#endif
#if CORE0_FEAT_SELFTEST
#  include "selftest.h"
#endif

// Largest payload a migration can read back (old version's bytes). Board may override.
#ifndef CORE0_SETTINGS_MAX_PAYLOAD
#  define CORE0_SETTINGS_MAX_PAYLOAD 256
#endif

namespace core0 {
namespace settings {
namespace {

const uint16_t kMagic = 0x5A43;  // 'ZC'

Binding  bind_ = {nullptr, 0, 0, nullptr, nullptr, 3000, 0};
bool     bound = false;
bool     dirty_ = false;
int8_t   active_slot = -1;  // slot holding the current data, -1 = none valid
uint32_t seq = 0;
TimerId  commit_timer = kNoTimer;
uint8_t  old_buf[CORE0_SETTINGS_MAX_PAYLOAD];

size_t slot_size() { return hal::nvm_size() / 2; }
size_t slot_offset(int8_t slot) { return slot == 0 ? 0 : slot_size(); }
size_t capacity() { return slot_size() - sizeof(SettingsHeader); }

uint16_t crc_of_slot(size_t off, uint16_t size) {
  uint16_t crc = 0xFFFF;
  uint8_t chunk[32];
  for (uint16_t done = 0; done < size;) {
    const uint16_t left = static_cast<uint16_t>(size - done);
    const uint16_t n = left > static_cast<uint16_t>(sizeof(chunk)) ? static_cast<uint16_t>(sizeof(chunk)) : left;
    if (!ok(hal::nvm_read(off + sizeof(SettingsHeader) + done, chunk, n))) return 0;
    crc = crc16_ccitt(chunk, n, crc);
    done = static_cast<uint16_t>(done + n);
  }
  return crc;
}

bool read_valid(int8_t slot, SettingsHeader& h) {
  const size_t off = slot_offset(slot);
  if (!ok(hal::nvm_read(off, &h, sizeof(h)))) return false;
  if (h.magic != kMagic || h.size == 0 || h.size > capacity()) return false;
  return crc_of_slot(off, h.size) == h.crc;
}

void apply_defaults() {
  if (bind_.apply_defaults) bind_.apply_defaults(bind_.data);
}

void post(SettingsCode c, uint32_t data = 0) {
  bus::post(make_event(EventType::Settings, static_cast<uint8_t>(c), 0, data));
}

Status write_slot() {
  const int8_t target = active_slot == 0 ? 1 : 0;
  const size_t off = slot_offset(target);
  SettingsHeader h;
  h.magic = kMagic;
  h.version = bind_.version;
  h.size = bind_.size;
  h.crc = crc16_ccitt(bind_.data, bind_.size);
  h.seq = seq + 1;
  Status s = hal::nvm_write(off + sizeof(SettingsHeader), bind_.data, bind_.size);  // payload first
  if (ok(s)) s = hal::nvm_write(off, &h, sizeof(h));                                // header last
  if (ok(s)) s = hal::nvm_commit();
  if (!ok(s)) return s;
  active_slot = target;
  seq = h.seq;
  return Status::Ok;
}

// The ONLY flash-writing path (constitution I exception): commit timer or on_suspend.
void commit() {
  if (!bound || !dirty_) return;
  const Status s = write_slot();
  if (ok(s)) {
    dirty_ = false;
    C0_LOGI("set", "saved seq=%lu slot=%c", static_cast<unsigned long>(seq), active_slot == 0 ? 'A' : 'B');
    post(SettingsCode::Saved, seq);
  } else {
    C0_LOGE("set", "save failed (%u)", static_cast<unsigned>(s));
    post(SettingsCode::SaveFailed, static_cast<uint32_t>(s));
  }
}

void on_commit_timer(TimerId, void*) { commit(); }

void arm_commit(ms_t delay) {
  if (commit_timer == kNoTimer) {
    commit_timer = timers::start(delay, TimerMode::OneShot, on_commit_timer);
  } else {
    timers::change_period(commit_timer, delay);
  }
}

#if CORE0_FEAT_CLI
void cmd_set(uint8_t argc, char** argv) {
  if (argc > 1 && strcmp(argv[1], "reset") == 0) {
    factory_reset();
    cli::print("factory reset: defaults applied, commit on next pass");
    return;
  }
  const bool redact = bind_.redact_from > 0 && bind_.redact_from < bind_.size;
  char tail[24];
  tail[0] = 0;
  if (redact) snprintf(tail, sizeof(tail), "  redacted from %u", static_cast<unsigned>(bind_.redact_from));
  cli::print("version %u seq %lu size %u slot %c%s%s", static_cast<unsigned>(bind_.version),
             static_cast<unsigned long>(seq), static_cast<unsigned>(bind_.size),
             active_slot < 0 ? '-' : (active_slot == 0 ? 'A' : 'B'), dirty_ ? " (dirty)" : "", tail);
  const uint8_t* p = static_cast<const uint8_t*>(bind_.data);
  char line[3 * 16 + 1];
  for (uint16_t i = 0; p && i < bind_.size; i = static_cast<uint16_t>(i + 16)) {
    size_t n = 0;
    for (uint16_t j = i; j < bind_.size && j < i + 16; ++j) {
      if (redact && j >= bind_.redact_from) n += snprintf(line + n, sizeof(line) - n, "** ");
      else n += snprintf(line + n, sizeof(line) - n, "%02X ", p[j]);
    }
    cli::print("  %s", line);
  }
}
const CliCommand kCmdSet = {"set", "set [dump|reset]: settings blob", cmd_set};
bool             cli_added = false;
#endif

#if CORE0_FEAT_SELFTEST
// `nvm`: a scratch pattern in the unused tail of slot B, committed through the normal commit path
// (constitution I: this stays the only flash-writing path). The port reads back from its RAM copy,
// so the commit status is what proves the flash: `dirty` stays set if the commit failed.
const ms_t    kNvmTestTimeoutMs = 2000;
const uint8_t kNvmPattern[4] = {0xA5, 0x5A, 0xC3, 0x3C};
uint8_t       nvm_saved[sizeof(kNvmPattern)];
ms_t          nvm_started = 0;

size_t nvm_scratch_offset() { return hal::nvm_size() - sizeof(kNvmPattern); }

void nvm_restore() { hal::nvm_write(nvm_scratch_offset(), nvm_saved, sizeof(nvm_saved)); }

TestResult nvm_step(ms_t now, const Event&, char* msg, uint8_t n) {
  if (n == 0) {
    nvm_restore();
    return TestResult::Fail;
  }
  if (msg[0] == 0) {
    if (!bound || capacity() < bind_.size + sizeof(kNvmPattern)) {
      snprintf(msg, n, "no-room");
      return TestResult::Fail;
    }
    if (!ok(hal::nvm_read(nvm_scratch_offset(), nvm_saved, sizeof(nvm_saved))) ||
        !ok(hal::nvm_write(nvm_scratch_offset(), kNvmPattern, sizeof(kNvmPattern))) || !ok(save_now())) {
      snprintf(msg, n, "write");
      return TestResult::Fail;
    }
    nvm_started = now;
    snprintf(msg, n, "Saving");
    return TestResult::Running;
  }
  if (dirty_) {
    if (time_since(now, nvm_started) < kNvmTestTimeoutMs) return TestResult::Running;
    nvm_restore();
    snprintf(msg, n, "commit");
    return TestResult::Fail;
  }
  uint8_t back[sizeof(kNvmPattern)];
  const bool same = ok(hal::nvm_read(nvm_scratch_offset(), back, sizeof(back))) &&
                    memcmp(back, kNvmPattern, sizeof(back)) == 0;
  nvm_restore();
  if (same) msg[0] = 0;
  else snprintf(msg, n, "readback");
  return same ? TestResult::Pass : TestResult::Fail;
}
const SelfTest kNvmTest = {"nvm", false, nvm_step};
#endif

Status init() {
  if (!bound) {
    C0_LOGE("set", "init failed: not bound");
    return Status::Invalid;
  }
  if (bind_.size > capacity()) {
    C0_LOGE("set", "init failed: %u B payload > %u B slot", static_cast<unsigned>(bind_.size),
            static_cast<unsigned>(capacity()));
    return Status::NoSpace;
  }
#if CORE0_FEAT_CLI
  if (!cli_added) cli_added = ok(cli::add(kCmdSet));
#endif
#if CORE0_FEAT_SELFTEST
  selftest::register_test(kNvmTest);
#endif
  load();
  C0_LOGI("set", "init ok (v%u, %u B, seq %lu)", static_cast<unsigned>(bind_.version),
          static_cast<unsigned>(bind_.size), static_cast<unsigned long>(seq));
  return Status::Ok;
}

void on_suspend(PowerState) { commit(); }  // flush dirty data before any lower state

}  // namespace

Status bind(const Binding& b) {
  if (!b.data || b.size == 0) return Status::Invalid;
  bind_ = b;
  bound = true;
  return Status::Ok;
}

Status load() {
  if (!bound) return Status::Invalid;
  SettingsHeader ha, hb;
  const bool va = read_valid(0, ha), vb = read_valid(1, hb);
  active_slot = -1;
  seq = 0;
  dirty_ = false;
  if (!va && !vb) {
    apply_defaults();
    C0_LOGW("set", "defaulted (no valid slot)");
    post(SettingsCode::Defaulted);
    return Status::Ok;
  }
  const int8_t slot = (va && vb) ? (static_cast<int32_t>(ha.seq - hb.seq) >= 0 ? 0 : 1) : (va ? 0 : 1);
  const SettingsHeader& h = slot == 0 ? ha : hb;
  active_slot = slot;
  seq = h.seq;
  const size_t payload = slot_offset(slot) + sizeof(SettingsHeader);
  if (h.version == bind_.version && h.size == bind_.size) {
    hal::nvm_read(payload, bind_.data, bind_.size);
    post(SettingsCode::Loaded, bind_.version);
    return Status::Ok;
  }
  // Version or size changed: defaults first, then migrate the old bytes over them.
  apply_defaults();
  bool migrated = false;
  if (h.size <= sizeof(old_buf) && ok(hal::nvm_read(payload, old_buf, h.size))) {
    if (bind_.migrate) {
      migrated = bind_.migrate(bind_.data, old_buf, h.size, h.version);
    } else if (h.version < bind_.version) {
      // Default migration: fields appended at the end — keep the old prefix.
      memcpy(bind_.data, old_buf, h.size < bind_.size ? h.size : bind_.size);
      migrated = true;
    }
  }
  if (migrated) {
    C0_LOGI("set", "migrated v%u -> v%u", static_cast<unsigned>(h.version), static_cast<unsigned>(bind_.version));
    post(SettingsCode::Migrated, (static_cast<uint32_t>(h.version) << 16) | bind_.version);
    mark_dirty();  // persist the migrated layout
  } else {
    apply_defaults();
    C0_LOGW("set", "v%u -> v%u not migrated: defaults", static_cast<unsigned>(h.version),
            static_cast<unsigned>(bind_.version));
    post(SettingsCode::Defaulted);
  }
  return Status::Ok;
}

void mark_dirty() {
  dirty_ = true;
  arm_commit(bind_.commit_delay_ms);
}

Status save_now() {
  if (!bound) return Status::Invalid;
  dirty_ = true;
  arm_commit(0);  // commit on the next loop pass (constitution I exception)
  return Status::Ok;
}

Status factory_reset() {
  if (!bound) return Status::Invalid;
  apply_defaults();
  C0_LOGI("set", "factory reset");
  return save_now();
}

bool dirty() { return dirty_; }

const Module kModule = {"settings", init, nullptr, nullptr, nullptr, on_suspend, nullptr};

}  // namespace settings

#if defined(CORE0_NATIVE)
namespace test {
void reset_settings() {
  if (settings::commit_timer != kNoTimer) timers::stop(settings::commit_timer);
  settings::bound = false;
  settings::dirty_ = false;
  settings::active_slot = -1;
  settings::seq = 0;
  settings::commit_timer = kNoTimer;
#if CORE0_FEAT_CLI
  settings::cli_added = false;
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_SETTINGS
