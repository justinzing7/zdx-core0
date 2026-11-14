// ZDX CORE-0 — build identity.
//
// The app defines exactly one BuildInfo (usually in main.cpp). Values come
// from build_flags so the binary always knows what it is:
//   build_flags = -DFW_VERSION=\"0.3.1\" -DBOARD_REV=\"B\"
//   extra_scripts = pre:tools/git_rev.py   (adds -DGIT_SHA=\"abc1234\")
#pragma once
#include "config.h"

#ifndef FW_VERSION
#  define FW_VERSION "0.0.0-dev"
#endif
#ifndef BOARD_REV
#  define BOARD_REV "?"
#endif
#ifndef GIT_SHA
#  define GIT_SHA "nogit"
#endif

#define CORE0_STR2_(x) #x
#define CORE0_STR_(x) CORE0_STR2_(x)
#define CORE0_VERSION_STRING \
  CORE0_STR_(CORE0_VERSION_MAJOR) "." CORE0_STR_(CORE0_VERSION_MINOR) "." CORE0_STR_(CORE0_VERSION_PATCH)

namespace core0 {

struct BuildInfo {
  const char* product;     // "ZINGCORE ..."
  const char* model;       // "ZT-406"
  const char* fw_version;  // FW_VERSION
  const char* board_rev;   // BOARD_REV
  const char* git_sha;     // GIT_SHA
  const char* build_date;  // __DATE__ " " __TIME__
  const char* core_version;// CORE0_VERSION_STRING
};

// Defined by the application.
extern const BuildInfo kBuildInfo;

}  // namespace core0

// Convenience for main.cpp:
//   C0_DEFINE_BUILD_INFO("ZINGCORE SOMNUS", "SN-1");
#define C0_DEFINE_BUILD_INFO(product, model)                                    \
  namespace core0 {                                                             \
  const BuildInfo kBuildInfo = {product, model, FW_VERSION, BOARD_REV, GIT_SHA, \
                                __DATE__ " " __TIME__, CORE0_VERSION_STRING};   \
  }
