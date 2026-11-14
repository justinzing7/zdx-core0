# Audio

Header: `audio.h` · Event type: `EventType::Audio` · Flags: `CORE0_FEAT_AUDIO`, `CORE0_FEAT_AUDIO_STREAM`, `CORE0_FEAT_AUDIO_IN`

## Tiers

| Tier | Flag | Hardware | Use |
|---|---|---|---|
| 1 Tone/Melody | `FEAT_AUDIO` | passive buzzer (PWM), active buzzer, DAC, I2S amp | key clicks, notifications, alarms |
| 2 Stream | `FEAT_AUDIO_STREAM` | DAC, I2S amp (MAX98357A) | MP3/WAV playback, voice prompts |
| 3 Capture | `FEAT_AUDIO_IN` | I2S/PDM mic (SPH0655, INMP441) | recording, level meter |

The output driver implements `AudioOut`. For tones on an I2S amp, the driver synthesizes a square or sine wave into its own buffer, so the tone API is the same on every board.

## Arbitration

Only one output plays at a time. A request at priority P:

| Currently playing | New request | Result |
|---|---|---|
| nothing | any | plays |
| UiClick | any | replaces |
| Notify | UiClick | dropped |
| Notify | Notify / Alarm / Media | replaces (`Preempted` for the old one) |
| Media | UiClick / Notify | dropped (product may instead duck/mix; see note) |
| Media | Alarm | Alarm plays; Media **pauses** (source position kept), resumes after |
| Alarm | anything but Alarm | dropped |

Muting (`set_muted`) silences UiClick and Notify only. Alarms ignore mute, because an alarm clock must ring.

Mixing clicks over media is out of scope for the foundation. A product that needs it implements a mixing `SampleSource`.

## Streaming

- `audio::stream(src)` opens the output at `src.sample_rate()`. In `tick()`, the service pulls from `SampleSource::read()` into an `SpscRing<int16_t, CORE0_AUDIO_RING_SAMPLES>`.
- The driver drains the ring with `audio::drain()` from its DMA/I2S callback, ISR, or core 1. If the ring runs dry, the driver outputs silence and the service posts `Underrun` (rate-limited).
- Ring sizing rule: ring duration must be at least 3× the worst-case loop pass. For example, 1024 samples at 16 kHz is 64 ms.
- **Placement:**
  - SUPERLOOP: the pump runs in tick, so the loop must stay responsive.
  - RTOS: the pump may run in its own task (the ESP32 I2S driver blocks on write).
  - DUALCORE: the pump runs on core 1.
  - In every case, decoders run where the pump runs.
- The output holds `PowerLock(Idle)` while playing, so the device never sleeps mid-sound. The amp's shutdown pin goes low (via `set_power(false)`) once playback has been idle for 200 ms, avoiding pops.

## Capture

- `capture_start(sink, rate)`: the driver fills its DMA buffers, `tick()` moves blocks to `SampleSink::write()`, and lost data posts `CaptureOverrun`.
- Capture holds `PowerLock(Idle)`.
- The sink (for example a WAV writer on storage) must keep up. The recorder feature specifies the buffer depth needed to cover SD write latency spikes; plan for 100–250 ms.

## Volume

`set_volume(0..255)` is applied in the driver: either hardware gain or a software scale on the stream path. The UI maps a 0–10 setting to a perceptual curve (squared).

## Acceptance

- [ ] A key click starts within 15 ms of Press.
- [ ] An Alarm during Media pauses Media; Media resumes at the same position after the Alarm stops.
- [ ] A 10-minute stream at 16 kHz has zero underruns on target with normal UI use.
- [ ] Amp shutdown asserts after playback ends (no idle hiss / current).
- [ ] Capture → storage for 10 minutes has zero overruns (products with `FEAT_AUDIO_IN`).
