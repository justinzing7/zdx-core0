// ============================================================================
//  board_config.h — Alarm Clock / Dream Recorder, carrier rev A
//  FireBeetle 2 ESP32-E + SSD1306 + MAX98357A + SPH0655 + RC522 + WS2812B
//
//  !! Every PIN_* marked TODO must be checked against YOUR carrier schematic.
//  The values below are placeholders chosen to avoid ESP32 strapping/flash pins.
// ============================================================================
#pragma once

#define CORE0_PROFILE CORE0_PROFILE_RTOS

// ---- Features -------------------------------------------------------------------
#define CORE0_FEAT_ENCODER      0
#define CORE0_FEAT_AUDIO_STREAM 1   // MAX98357A playback (alarm tones, recordings)
#define CORE0_FEAT_AUDIO_IN     1   // SPH0655 capture (dream recorder)
#define CORE0_FEAT_STORAGE      1   // recordings (backend: open question, see PILOT.md)
#define CORE0_FEAT_NET          1   // NTP time sync only
#define CORE0_FACTORY_COMBO     { ::core0::Key::Up, ::core0::Key::Select }

#define APP_FEAT_NFC            1

// ---- Sizing -----------------------------------------------------------------------
#define CORE0_EVENT_QUEUE_LEN    64
#define CORE0_AUDIO_RING_SAMPLES 2048   // 128 ms @ 16 kHz: covers loop + flash write jitter
#define REC_SAMPLE_RATE          16000

// ---- Buttons (TODO: verify) ---------------------------------------------------------
#define PIN_BTN_UP       25   // TODO
#define PIN_BTN_SELECT   26   // TODO
#define PIN_BTN_DOWN     27   // TODO  (RTC GPIOs: usable for ext1 wake from deep sleep)

// ---- I2C: SSD1306 (TODO: verify) ------------------------------------------------------
#define PIN_I2C_SDA      21   // FireBeetle default SDA (TODO)
#define PIN_I2C_SCL      22   // FireBeetle default SCL (TODO)
#define OLED_I2C_ADDR    0x3C

// ---- I2S0 out: MAX98357A (TODO: verify) ------------------------------------------------
#define PIN_AMP_BCLK     14   // TODO
#define PIN_AMP_LRCLK    13   // TODO
#define PIN_AMP_DIN      4    // TODO
#define PIN_AMP_SD       0xFF // shutdown/enable pin if wired (TODO) — used by AudioOut::set_power

// ---- I2S1 in: SPH0655 (TODO: verify) ------------------------------------------------------
#define PIN_MIC_BCLK     16   // TODO
#define PIN_MIC_WS       17   // TODO
#define PIN_MIC_DATA     35   // TODO (input-only pin is fine for data)

// ---- SPI: RC522 (TODO: verify) ---------------------------------------------------------
#define PIN_NFC_SCK      18   // VSPI default
#define PIN_NFC_MISO     19
#define PIN_NFC_MOSI     23
#define PIN_NFC_SS       5    // TODO (strapping pin: OK as CS with pull-up)
#define PIN_NFC_RST      33   // TODO — held LOW = hard power-down between alarms

// ---- LED ----------------------------------------------------------------------------
#define PIN_LED_DATA     32   // TODO (WS2812B; avoid GPIO12 — strapping pin sets flash voltage)
#define PIN_LED_PWR_EN   0xFF // switch for LED supply, 0xFF = not switchable

// ---- Battery (TODO: confirm FireBeetle 2 ESP32-E battery sense pin + ratio) -----------
#define PIN_BATT_ADC     34   // TODO verify on DFRobot schematic
#define PIN_BATT_EN      0xFF
#define BATT_DIVIDER_NUM 2    // TODO verify
#define BATT_DIVIDER_DEN 1
#define BATT_LOW_MV      3500
#define BATT_CRIT_MV     3350

// ---- Display ---------------------------------------------------------------------------
#define DISPLAY_W 128
#define DISPLAY_H 64

// ---- Logical key map: 3-button convention (see PILOT.md) ---------------------------------
#define BOARD_KEYS                                                                             \
  {                                                                                            \
    {::core0::Key::Up,     PIN_BTN_UP,     ::core0::kKeyActiveLow | ::core0::kKeyRepeat | ::core0::kKeyWake},     \
    {::core0::Key::Down,   PIN_BTN_DOWN,   ::core0::kKeyActiveLow | ::core0::kKeyRepeat | ::core0::kKeyWake},     \
    {::core0::Key::Select, PIN_BTN_SELECT, ::core0::kKeyActiveLow | ::core0::kKeyLongPress | ::core0::kKeyWake},  \
  }
