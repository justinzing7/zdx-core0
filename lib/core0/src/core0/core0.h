// ZDX CORE-0 — umbrella header.  #include <core0/core0.h>
//
// Pulls in every module whose feature flag is enabled (config.h /
// board_config.h). Spec: docs/core0/spec/00-architecture.md
#pragma once

#include "config.h"
#include "types.h"
#include "port.h"
#include "ring.h"
#include "event.h"
#include "module.h"
#include "timer.h"
#include "log.h"
#include "fault.h"
#include "version.h"

#if CORE0_FEAT_INPUT
#  include "input.h"
#endif
#if CORE0_FEAT_DISPLAY
#  include "display.h"
#endif
#if CORE0_FEAT_UI
#  include "ui.h"
#  include "anim.h"
#endif
#if CORE0_FEAT_POWER
#  include "power.h"
#endif
#if CORE0_FEAT_BATTERY
#  include "battery.h"
#endif
#if CORE0_FEAT_AUDIO
#  include "audio.h"
#endif
#if CORE0_FEAT_FEEDBACK
#  include "feedback.h"
#endif
#if CORE0_FEAT_SETTINGS
#  include "settings.h"
#endif
#if CORE0_FEAT_CLOCK
#  include "clock.h"
#endif
#if CORE0_FEAT_CLI
#  include "cli.h"
#endif
#if CORE0_FEAT_SELFTEST
#  include "selftest.h"
#endif
#if CORE0_FEAT_STORAGE
#  include "storage.h"
#endif
#if CORE0_FEAT_NET
#  include "net.h"
#endif
