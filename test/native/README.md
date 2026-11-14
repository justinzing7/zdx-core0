# Foundation tests

Every `test_core0_*` suite here maps to acceptance criteria in `spec/modules/*.md`. They run on the host:

```sh
pio test -e native_core0
```

Rules for a foundation suite:
- Include only `test_helpers.h` and the generic mocks (`mock_panel.h`, `mock_audio.h`, `mock_led.h`, `mock_rtc.h`).
  No product headers, pins or product names.
- Put `#include <unity.h>` in the test `.cpp` itself. PlatformIO only adds Unity when the test source includes it.

The kit lives in `lib/core0_testkit/` (host builds only): the native port (`port/port_native.*`, a controllable clock,
NVM power-cut injection, console capture and a `setjmp` reset point), `test_helpers.h` and the mocks. A product's own
host tests use the same kit through the submodule, so there is one copy.

The suites run against `kit/include/board_config.h`, which has the pilot product's sizing. Pure defaults and other
boards are covered by the three-config compile check (see `CHANGELOG.md` 0.1.0).
