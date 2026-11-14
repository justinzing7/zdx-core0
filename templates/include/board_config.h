// ============================================================================
//  board_config.h — <PRODUCT> rev <X>
//  The ONLY place pins, wiring and feature selection live.
//  Picked up automatically by core0/config.h (project include/ is on the path).
//  One file per board revision: include/boards/<rev>.h selected by BOARD_REV_<X>.
// ============================================================================
#pragma once

// ---- Execution profile -------------------------------------------------------
// SUPERLOOP (SAMD21/STM32) | RTOS (ESP32) | DUALCORE (RP2040, ESP32 w/ core-1 audio)
#define CORE0_PROFILE CORE0_PROFILE_SUPERLOOP

// ---- Features (1 = on) ---------------------------------------------------------
#define CORE0_FEAT_ENCODER      0
#define CORE0_FEAT_AUDIO_STREAM 0
#define CORE0_FEAT_AUDIO_IN     0
#define CORE0_FEAT_STORAGE      0
#define CORE0_FEAT_NET          0

// ---- Sizing overrides (only if defaults don't fit) ------------------------------
// #define CORE0_EVENT_QUEUE_LEN 64
// #define CORE0_LOG_LEVEL CORE0_LOG_DEBUG

// ---- Pins ----------------------------------------------------------------------
#define PIN_BTN_UP      0xFF   // <set>
#define PIN_BTN_DOWN    0xFF
#define PIN_BTN_SELECT  0xFF
#define PIN_BATT_ADC    0xFF
#define PIN_BATT_EN     0xFF   // divider enable, 0xFF = always connected
#define PIN_BUZZER      0xFF
#define PIN_LED         0xFF
#define PIN_I2C_SDA     0xFF
#define PIN_I2C_SCL     0xFF

// ---- Logical key map --------------------------------------------------------------
// Used in main.cpp:  input::configure(kBoardKeys, count_of(kBoardKeys));
#define BOARD_KEYS                                                                 \
  {                                                                                \
    {::core0::Key::Up,     PIN_BTN_UP,     ::core0::kKeyActiveLow | ::core0::kKeyRepeat | ::core0::kKeyWake}, \
    {::core0::Key::Down,   PIN_BTN_DOWN,   ::core0::kKeyActiveLow | ::core0::kKeyRepeat | ::core0::kKeyWake}, \
    {::core0::Key::Select, PIN_BTN_SELECT, ::core0::kKeyActiveLow | ::core0::kKeyLongPress | ::core0::kKeyWake}, \
  }

// ---- Battery ------------------------------------------------------------------------
#define BATT_DIVIDER_NUM 2     // e.g. 100k/100k divider -> x2
#define BATT_DIVIDER_DEN 1
#define BATT_LOW_MV      3500
#define BATT_CRIT_MV     3350

// ---- Display --------------------------------------------------------------------------
#define DISPLAY_W 128
#define DISPLAY_H 64
