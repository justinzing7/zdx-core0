// board_config.h for the ZDX CORE-0 test kit (env:native_core0 in this repo).
//
// Sizing and features match the pilot product (zingcore_remtrak, carrier rev A), where these
// suites were first validated. There are no pins: the foundation never needs them. Pure-default
// and other-board configs are covered by the three-config compile check instead.
#pragma once

#define CORE0_PROFILE CORE0_PROFILE_RTOS

#define CORE0_FEAT_ENCODER      0
#define CORE0_FEAT_AUDIO_STREAM 1
#define CORE0_FEAT_AUDIO_IN     1
#define CORE0_FEAT_STORAGE      0
#define CORE0_FEAT_NET          0
#define CORE0_FACTORY_COMBO     { ::core0::Key::Up, ::core0::Key::Select }
#define CORE0_INPUT_POLL_GPIO   0

#define CORE0_EVENT_QUEUE_LEN    64
#define CORE0_AUDIO_RING_SAMPLES 2048
