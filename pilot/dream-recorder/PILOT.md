# Pilot: Alarm Clock / Dream Recorder on ZDX CORE-0

**Hardware:**
- FireBeetle 2 ESP32-E on a custom 2-layer carrier PCB.
- SSD1306 OLED, 3 buttons, WS2812B status LED.
- MAX98357A I2S amp with an 8 Ω speaker.
- SPH0655 I2S MEMS mic.
- RC522 (MFRC522) NFC on an eval board.
- 1S LiPo.

**Profile:** `RTOS`. The audio pump and capture may run in their own FreeRTOS tasks; everything else runs in the loop.

The pilot has two goals: ship the device, and **finish CORE-0**, meaning implement the services and the ESP32 port and fold the lessons back into the spec.

## Foundation mapping (pre-filled for `/speckit-plan`)

| Module | Use / Extend / Change / Off | Driver / implementation | Notes |
|---|---|---|---|
| core, port | Use (implement) | `port_esp32.cpp`, `port_native.cpp` | First real port; becomes the reference |
| input | Use | GPIO poll, 3 keys → Up / Down / Select | Back = Select LongPress on 3-button UI (see below) |
| display | Use | U8g2 `SSD1306_128X64_NONAME_F_HW_I2C` → `Panel`/`Gfx` adapter | Burn-in shift for clock face |
| ui / anim | Use + Extend | — | Clock face, alarm-ringing screen, recorder screen, library list |
| power | Use | ESP32 light sleep (Sleep), deep sleep (Off) | Alarm needs RTC wake; recording holds `PowerLock(Idle)` |
| battery | Use | ADC on FireBeetle battery sense pin (**verify on schematic**) | LiPo curve; charge-status pin if wired |
| audio | Use (tone + **stream** + **capture**) | `I2sOut` (MAX98357A, I2S0), `I2sMicIn` (SPH0655, I2S1) | Two I2S peripherals avoid shared-clock constraints |
| feedback | Use | WS2812B via RMT (1 LED) | Switch LED supply off in Sleep if possible (~1 mA quiescent) |
| settings | Use | ESP32 `Preferences` blob | Alarm time(s), volume, tone, NFC tag UID(s), tz |
| clock | Use | ESP32 RTC timer + NTP via Wi-Fi | **Open question:** add DS3231 for accuracy and power-loss survival? |
| diagnostics | Use | — | CLI: `alarm`, `nfc`, `rec` commands |
| storage | Use (likely) | **Open question:** SD vs LittleFS on flash | Dream recordings are the bulk data |
| net | Use (minimal) | ESP32 Wi-Fi, acquired only for NTP sync | Off otherwise |
| app: alarm | New | — | Schedule, ring, snooze, escalation |
| app: nfc_dismiss | New | MFRC522 over SPI, RST pin for hard power-down | Posts `EventType::Sensor` with tag UID hash |
| app: recorder | New | — | Arm → record → save; on-wake "record dream" quick action |

### Three-button UI convention (CORE-0 recommendation)
| Gesture | Meaning |
|---|---|
| Up / Down Click / Repeat | move / adjust |
| Select Click | enter / confirm |
| Select LongPress | Back |
| Up+Down Combo | Menu / home |
| Any key while ringing | snooze (only **NFC tag** dismisses) |

## Proposed Spec Kit feature sequence

| # | Feature | Spec Kit `/speckit-specify` seed |
|---|---|---|
| 001 | **Foundation bring-up** | QUICKSTART §5 text: splash, settings menu, clicks, LED, battery, sleep/wake, CLI, self-test. Implements `lib/core0/src/*.cpp` + ESP32/native ports |
| 002 | Clock face + time | Large time display, date, battery/alarm icons; time set via menu; NTP sync when Wi-Fi configured; correct after sleep |
| 003 | Alarm | Set alarm time(s) & days; wakes from sleep; ramping volume; snooze; ringing screen; survives reboot |
| 004 | NFC dismiss | Alarm only stops when the registered tag is scanned; tag enrollment in settings; fallback if reader fails (safety: e.g., auto-stop after N min) |
| 005 | Dream recorder | One-press record on waking (from alarm screen or any time); recording indicator; saves with timestamp; storage-full handling |
| 006 | Playback & library | Browse recordings by date, play, delete |
| 007 | Power tuning | Measure every state, hit the battery-life target, fill the power budget |

The order matters: 001 makes CORE-0 real, and 002–004 exercise power, clock, settings, and audio priority (Alarm preempting everything, alarms ignoring mute). 005 stresses capture-to-storage, which is the hardest real-time path.

## Open questions to settle in 001/002 plans

1. **Recording storage.** At 16 kHz/16-bit mono, the ESP32-E's 4 MB flash holds only about 1.5 min of raw PCM. Options:
   - an SD card on the carrier;
   - external SPI flash;
   - IMA-ADPCM (4:1, about 6 min on internal flash);
   - 8 kHz (speech-quality) audio.
2. **Timekeeping.** The ESP32 RTC on the internal RC oscillator drifts noticeably over days. Does the FireBeetle have the 32 kHz crystal populated? If not, the options are a DS3231 or NTP sync each night.
3. **Battery sense pin and divider ratio** on the FireBeetle 2 ESP32-E: confirm from the DFRobot schematic.
4. **Sleep current target.** Is the RC522 hard-powered-down (RST low) between alarms? Can the WS2812B supply be switched?
5. **Alarm safety fallback** if NFC fails: auto-stop after N minutes, or a button-hold override?

## First session in Claude Code (after QUICKSTART steps 1–3)

```text
/speckit-constitution Project: <ZINGCORE name> alarm clock / dream recorder. MCU/board: FireBeetle 2
  ESP32-E on custom carrier. Profile: RTOS. Power: 1S LiPo. Owner: Zingcore Dynamics.
  Foundation: ZDX CORE-0 0.1.0.

/speckit-specify Bring up the ZDX CORE-0 foundation on this board: boot banner over serial, splash
  screen, three-button navigation of a settings menu that persists across reboot, key-click sounds
  through the speaker, status LED patterns, battery readout, and dim/idle/sleep with wake on button.
  Include a factory self-test for the OLED, LED, speaker, mic and NFC reader presence.

/speckit-plan PlatformIO + Arduino (espressif32) on dfrobot_firebeetle2_esp32e. Implement the
  foundation services in lib/core0/src and the ESP32 + native ports per docs/core0. Use U8g2 for the
  SSD1306, ESP32 I2S driver for MAX98357A (I2S0) and SPH0655 (I2S1), Adafruit NeoPixel or RMT for the
  WS2812B, Preferences for NVM. Pilot board config is include/board_config.h.
```
