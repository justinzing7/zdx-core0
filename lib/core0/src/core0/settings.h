// ZDX CORE-0 — persistent settings.
//
// The app defines ONE plain struct (POD, fixed-size fields, no pointers) and a
// version number. The service stores it in two NVM slots (A/B) with a header
// {magic, version, size, seq, crc16}. Load picks the valid slot with the
// higher sequence; save writes the OTHER slot, so a power cut mid-write never
// loses the last good copy.
//
// Wear: saves are deferred — mark_dirty() starts a commit delay; changes made
// while scrolling a menu collapse into one write. Dirty data is also flushed
// on power suspend.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

struct SettingsHeader {
  uint16_t magic;     // 0x5A43 'ZC'
  uint16_t version;   // app struct version
  uint16_t size;      // sizeof(app struct)
  uint16_t crc;       // CRC-16/CCITT over payload
  uint32_t seq;       // increments every save
};

// Event codes for EventType::Settings.
enum class SettingsCode : uint8_t {
  Loaded,     // data = version
  Defaulted,  // no valid slot: defaults applied
  Migrated,   // data = (old_version << 16) | new_version
  Saved,
  SaveFailed,
};

inline uint16_t crc16_ccitt(const void* data, size_t len, uint16_t crc = 0xFFFF) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  while (len--) {
    crc ^= static_cast<uint16_t>(*p++) << 8;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
  }
  return crc;
}

namespace settings {

// Migration hook: given the old payload bytes (old_size, old_version), fill
// `dst` (already holding defaults). Return false to discard and keep defaults.
typedef bool (*MigrateFn)(void* dst, const void* old, uint16_t old_size, uint16_t old_version);

struct Binding {
  void*     data;
  uint16_t  size;
  uint16_t  version;
  void (*apply_defaults)(void* data);
  MigrateFn migrate;          // nullptr = defaults on version mismatch
  ms_t      commit_delay_ms;  // e.g. 3000
};

Status bind(const Binding& b);   // call before sys::begin(); load happens in init
Status load();
void   mark_dirty();             // schedule a deferred save
Status save_now();
Status factory_reset();          // defaults + save
bool   dirty();

extern const Module kModule;

}  // namespace settings

// Typed convenience wrapper:
//   struct AppSettings { int16_t volume; int16_t alarm_min; ... };
//   static Settings<AppSettings, 2> g_settings(defaults_fn);
template <typename T, uint16_t Version>
class Settings {
 public:
  explicit Settings(void (*defaults)(T&), settings::MigrateFn migrate = nullptr, ms_t commit_delay = 3000)
      : defaults_(defaults) {
    settings::Binding b = {&value, static_cast<uint16_t>(sizeof(T)), Version,
                           &Settings::apply_defaults_thunk, migrate, commit_delay};
    binding_ = b;
    instance_ = this;
  }
  Status bind() { return settings::bind(binding_); }
  T&     operator*() { return value; }
  T*     operator->() { return &value; }
  void   changed() { settings::mark_dirty(); }

  T value;

 private:
  static void apply_defaults_thunk(void* p) {
    if (instance_ && instance_->defaults_) instance_->defaults_(*static_cast<T*>(p));
  }
  void (*defaults_)(T&);
  settings::Binding binding_;
  static Settings* instance_;
};

template <typename T, uint16_t V>
Settings<T, V>* Settings<T, V>::instance_ = nullptr;

}  // namespace core0
