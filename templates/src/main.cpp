// ============================================================================
//  <PRODUCT NAME> firmware — built on ZDX CORE-0
//  Template: copy to src/main.cpp and fill in the <...> markers.
// ============================================================================
#include <core0/core0.h>

#if !defined(CORE0_NATIVE)
#  include <Arduino.h>
#endif

using namespace core0;

C0_DEFINE_BUILD_INFO("ZINGCORE <PRODUCT>", "<MODEL>");

// ---- Settings ---------------------------------------------------------------
// One POD struct. Bump the version when the layout changes and add a migrate fn.
struct AppSettings {
  int16_t volume;      // 0..10
  int16_t brightness;  // 0..10
  int16_t sleep_min;   // auto-sleep minutes
  int16_t sounds_on;   // 0/1
};
static void settings_defaults(AppSettings& s) {
  s.volume = 6; s.brightness = 8; s.sleep_min = 2; s.sounds_on = 1;
}
static Settings<AppSettings, 1> g_settings(settings_defaults);
static void on_setting_changed(int16_t) { g_settings.changed(); }

// ---- Screens ----------------------------------------------------------------
// Project screens live in src/app/screens/*.h; one static instance each.
// #include "app/screens/home_screen.h"
// static HomeScreen s_home;

// Settings menu: data-driven, lives in flash.
static const MenuItem kSettingsItems[] = {
  menu_range(1, "Volume",     &g_settings.value.volume,     0, 10, 1, on_setting_changed),
  menu_range(2, "Brightness", &g_settings.value.brightness, 0, 10, 1, on_setting_changed),
  menu_range(3, "Auto sleep", &g_settings.value.sleep_min,  1, 30, 1, on_setting_changed),
  menu_toggle(4, "Sounds",    &g_settings.value.sounds_on,  on_setting_changed),
  menu_back(),
};
static const Menu kSettingsMenu = {"SETTINGS", kSettingsItems, count_of(kSettingsItems)};
static MenuScreen s_settings(kSettingsMenu);

// static const SplashConfig kSplash = {&kBootAnim, nullptr, "ZINGCORE", 1200, 5000, &s_home};
// static SplashScreen s_splash(kSplash);

// ---- Drivers (project-specific, src/drivers/) --------------------------------
// static Ssd1306Panel   g_panel;     // implements core0::Panel
// static I2sAudioOut    g_audio_out; // implements core0::AudioOut

// ---- Boot ---------------------------------------------------------------------
void setup() {
  fault::init();                       // 1. always first: reset reason, safe mode

  // 2. register modules — order = init/tick order (suspend runs in reverse)
  sys::register_module(&power::kModule);
  sys::register_module(&input::kModule);
  sys::register_module(&battery::kModule);
  sys::register_module(&settings::kModule);
  sys::register_module(&clock::kModule);
  sys::register_module(&audio::kModule);
  sys::register_module(&feedback::kModule);
  sys::register_module(&display::kModule);
  sys::register_module(&ui::kModule);
  sys::register_module(&cli::kModule);
  // sys::register_module(&app::kModule);   // device features last

  // 3. bind configuration (board_config.h supplies pins/curves)
  g_settings.bind();
  // input::configure(kBoardKeys, count_of(kBoardKeys));
  // battery::configure(kBoardBattery);
  // display::attach(g_panel);
  // audio::attach_out(g_audio_out);

  // 4. bring everything up, then show the first screen
  sys::begin();
  // if (selftest::requested_at_boot()) selftest::run_all(); else ui::home(s_splash);
  ui::home(s_settings);  // placeholder until the home screen exists
}

void loop() {
  sys::run_once();   // never add delay() here
}
