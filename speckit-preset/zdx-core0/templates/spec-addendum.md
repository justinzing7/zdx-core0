
---

## Device Context *(mandatory — ZDX CORE-0)*

<!--
  Describe the feature from the device user's point of view. Stay technology-agnostic
  (no pins, libraries, or class names here — those belong in the plan).
-->

- **Product**: [ZINGCORE product name / model]
- **When is this feature used?**: [e.g., at bedtime, in the dark, half-awake]
- **Device state at start**: [Active / asleep / alarm ringing / on charger]
- **Feedback channels involved**: [display / sound / LED / haptic]

### Interaction Flow

<!--
  List screens and the inputs that move between them, using LOGICAL keys
  (Up, Down, Select, Back, Menu, Power, Aux1..3, Rotate) and gestures
  (Click, LongPress, DoubleClick, Repeat, Combo).
-->

| From screen | Input | Result |
|-------------|-------|--------|
| [Home] | [Select Click] | [Opens …] |

### Power & Timing Expectations

- **May the device sleep during this feature?**: [yes / no / only after …]
- **Response time expectations**: [e.g., recording must start within 0.5 s of the press]
- **Battery impact the user would accept**: [e.g., 8 h of recording on a full charge]
- **Behavior on low / critical battery during this feature**: [e.g., stop and save the recording]

### Failure Behavior

- [What the user sees if hardware is missing or fails (no SD card, mic fault, RTC lost time)]
- [What data must never be lost, e.g., a recording interrupted by power loss]

### Hardware Acceptance Criteria

<!--
  Measurable on the real device. Each becomes a verification task.
-->

- **HW-001**: [e.g., from button press to recording LED on ≤ 300 ms]
- **HW-002**: [e.g., average current while recording ≤ 45 mA]
