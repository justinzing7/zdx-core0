// ZDX CORE-0 — audio: tones/melodies (buzzer), sample streaming (DAC/I2S),
// and optional capture (microphone).
//
// Tier 1 (always):           tone + melody player. Works on a passive buzzer
//                            (PWM), an active buzzer (freq ignored), or a DAC.
// Tier 2 (FEAT_AUDIO_STREAM): pull-based PCM streaming through an SPSC ring:
//                            tick() pulls from a SampleSource into the ring,
//                            the driver drains it from DMA/ISR/core 1.
// Tier 3 (FEAT_AUDIO_IN):     capture pushes PCM blocks to a SampleSink in
//                            loop context (driver fills from DMA).
//
// One output at a time, arbitrated by priority. Playing holds a PowerLock at Idle.
#pragma once
#include "event.h"
#include "module.h"

namespace core0 {

enum class AudioPriority : uint8_t {
  UiClick = 0,  // key clicks — dropped if anything else is playing
  Notify  = 1,  // confirmations, timer done
  Alarm   = 2,  // alarm clock: preempts media
  Media   = 3,  // NOT preempted by Notify; Alarm still wins (see spec)
};

struct Note {
  uint16_t freq_hz;  // 0 = rest
  uint16_t ms;
};

struct Melody {
  const Note* notes;
  uint8_t     count;
  uint8_t     repeat;  // 0 = once, 0xFF = forever (until stop)
};

// PCM source for streaming (decoder, flash clip, SD file). Loop context only.
class SampleSource {
 public:
  virtual ~SampleSource() {}
  virtual uint32_t sample_rate() const = 0;
  // Fill up to n mono int16 samples; return count written. 0 = end of stream.
  virtual size_t read(int16_t* dst, size_t n) = 0;
};

// PCM sink for capture (recorder writing to storage, level meter...).
class SampleSink {
 public:
  virtual ~SampleSink() {}
  virtual size_t write(const int16_t* src, size_t n) = 0;  // returns accepted
};

// Output driver contract (buzzer PWM, DAC, I2S amp like MAX98357A).
class AudioOut {
 public:
  virtual ~AudioOut() {}
  virtual Status begin() = 0;
  virtual void   tone(uint16_t freq_hz) = 0;   // 0 = silence
  virtual bool   can_stream() const { return false; }
  virtual Status stream_start(uint32_t sample_rate) { (void)sample_rate; return Status::Unsupported; }
  virtual void   stream_stop() {}
  virtual void   set_volume(uint8_t v) = 0;    // 0..255 (gain or software scale)
  virtual void   set_power(bool on) = 0;       // amp shutdown pin / DAC off
};

// Input driver contract (I2S / PDM mic).
class AudioIn {
 public:
  virtual ~AudioIn() {}
  virtual Status begin(uint32_t sample_rate) = 0;
  virtual Status start() = 0;
  virtual void   stop() = 0;
  // Non-blocking: copy up to n captured samples; returns count.
  virtual size_t read(int16_t* dst, size_t n) = 0;
  virtual void   set_power(bool on) = 0;
};

// Event codes for EventType::Audio.  arg = AudioPriority
enum class AudioCode : uint8_t {
  Started, Finished, Preempted,
  Underrun,       // stream ring ran dry, data = count
  CaptureOverrun, // capture data lost, data = samples dropped
};

namespace audio {

Status attach_out(AudioOut& out);
#if CORE0_FEAT_AUDIO_IN
Status attach_in(AudioIn& in);
#endif

Status beep(uint16_t freq_hz, uint16_t ms, AudioPriority p = AudioPriority::UiClick);
Status play(const Melody& m, AudioPriority p = AudioPriority::Notify);
#if CORE0_FEAT_AUDIO_STREAM
Status stream(SampleSource& src, AudioPriority p = AudioPriority::Media);
#endif
void   stop(AudioPriority p);   // stops output if it is at priority <= p
bool   busy();
AudioPriority current();

void    set_volume(uint8_t v);
uint8_t volume();
void    set_muted(bool m);      // silences UiClick/Notify; Alarm ignores mute

#if CORE0_FEAT_AUDIO_IN
Status capture_start(SampleSink& sink, uint32_t sample_rate);
void   capture_stop();
bool   capturing();
#endif

#if CORE0_FEAT_AUDIO_STREAM
// Driver side: drain the stream ring (ISR / DMA callback / core 1).
size_t drain(int16_t* dst, size_t n);
#endif

extern const Module kModule;

}  // namespace audio
}  // namespace core0
