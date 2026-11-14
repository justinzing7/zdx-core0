// ZDX CORE-0 — audio service (audio.h, spec/modules/audio.md).
//
// One output at a time, arbitrated by priority (audio.md table). Tones/melodies are timed here
// (loop context); the driver synthesises. Streams: tick() pulls from the SampleSource into an
// SPSC ring that the driver drains (task/ISR/core 1). Capture: tick() moves blocks from the
// AudioIn to the SampleSink. Playing or capturing holds PowerLock(Idle). After playback the amp
// is powered down 200 ms later (drivers may keep it warm by their own policy).
#include "config.h"
#if CORE0_FEAT_AUDIO  // feature off = zero code (constitution IX)

#include "audio.h"

#include "log.h"
#include "port.h"
#include "power.h"
#include "ring.h"

namespace core0 {
namespace audio {
namespace {

const ms_t     kAmpOffDelayMs = 200;
const ms_t     kUnderrunReportMs = 1000;
const size_t   kPumpPerTick = 256;
const ms_t     kIdleDeadlineMs = 60000;

enum class Kind : uint8_t { None, Tone, Melody, Stream };

AudioOut* out = nullptr;
Kind          kind = Kind::None;
AudioPriority prio = AudioPriority::UiClick;
ms_t          end_at = 0;         // tone end / current note end
const Melody* mel = nullptr;
uint8_t       note_i = 0, repeats_done = 0;
uint8_t       vol = 255;
bool          muted = false;
bool          amp_on = false;
bool          amp_off_pending = false;
ms_t          amp_off_at = 0;
PowerLock     play_lock = kNoLock;

#if CORE0_FEAT_AUDIO_STREAM
SpscRing<int16_t, CORE0_AUDIO_RING_SAMPLES> ring;
SampleSource* src = nullptr;
bool          src_ended = false;
SampleSource* paused_src = nullptr;  // Media paused by an Alarm
volatile uint32_t underruns = 0;
uint32_t      underruns_reported = 0;
ms_t          underrun_report_at = 0;
bool          underrun_reported_once = false;
#endif

#if CORE0_FEAT_AUDIO_IN
AudioIn*    in = nullptr;
SampleSink* sink = nullptr;
uint32_t    in_rate = 0;
PowerLock   cap_lock = kNoLock;
#endif

void post(AudioCode c, AudioPriority p, uint32_t data = 0) {
  bus::post(make_event(EventType::Audio, static_cast<uint8_t>(c), static_cast<uint16_t>(p), data));
}

void amp_up() {
  amp_off_pending = false;
  if (!amp_on && out) {
    out->set_power(true);
    amp_on = true;
  }
}

// Stop whatever plays (no events). Keeps the lock if `keep_lock`.
void halt_output() {
  if (!out) return;
  out->tone(0);
#if CORE0_FEAT_AUDIO_STREAM
  if (kind == Kind::Stream) {
    out->stream_stop();
    src = nullptr;
    int16_t s;
    while (ring.pop(s)) {
    }
  }
#endif
  kind = Kind::None;
}

void lock_on() {
  if (play_lock == kNoLock) play_lock = power::lock(PowerState::Idle, "audio");
}

void finish(AudioCode why) {
  const AudioPriority p = prio;
  halt_output();
  post(why, p);
#if CORE0_FEAT_AUDIO_STREAM
  if (paused_src && why != AudioCode::Preempted) {  // resume Media after the Alarm
    SampleSource* s = paused_src;
    paused_src = nullptr;
    stream(*s, AudioPriority::Media);
    return;
  }
#endif
  power::unlock(play_lock);
  amp_off_pending = true;
  amp_off_at = hal::millis() + kAmpOffDelayMs;
}

// Arbitration (audio.md): may the request at priority p start now?
bool admit(AudioPriority p) {
  if (muted && (p == AudioPriority::UiClick || p == AudioPriority::Notify)) return false;
  if (kind == Kind::None) return true;
  switch (prio) {
    case AudioPriority::UiClick: return true;
    case AudioPriority::Notify: return p != AudioPriority::UiClick;
    case AudioPriority::Media: return p == AudioPriority::Alarm || p == AudioPriority::Media;
    case AudioPriority::Alarm: return p == AudioPriority::Alarm;
  }
  return false;
}

// Take over the output for a new sound at priority p (after admit()).
void take_over(AudioPriority p) {
  if (kind != Kind::None) {
#if CORE0_FEAT_AUDIO_STREAM
    if (kind == Kind::Stream && prio == AudioPriority::Media && p == AudioPriority::Alarm) {
      paused_src = src;  // pause, keep the source (its position) for resume
      out->stream_stop();
      int16_t s;
      while (ring.pop(s)) {
      }
      src = nullptr;
      kind = Kind::None;
    } else
#endif
    {
      const AudioPriority old = prio;
      halt_output();
      post(AudioCode::Preempted, old);
    }
  }
  prio = p;
  lock_on();
  amp_up();
}

void start_note(ms_t now) {
  const Note& n = mel->notes[note_i];
  out->tone(n.freq_hz);
  end_at = now + n.ms;
}

Status init() {
  if (!out) {
    C0_LOGE("aud", "init failed: no output attached");
    return Status::NotFound;
  }
  const Status s = out->begin();
  if (!ok(s)) {
    C0_LOGE("aud", "init failed: output begin (%u)", static_cast<unsigned>(s));
    return s;
  }
  out->set_volume(vol);
#if CORE0_FEAT_AUDIO_IN
  if (in) C0_LOGI("aud", "capture input attached");
#endif
  C0_LOGI("aud", "init ok (stream %s, ring %u samples)", out->can_stream() ? "yes" : "no",
          static_cast<unsigned>(CORE0_FEAT_AUDIO_STREAM ? CORE0_AUDIO_RING_SAMPLES : 0));
  return Status::Ok;
}

void tick(ms_t now) {
  if ((kind == Kind::Tone || kind == Kind::Melody) && time_reached(now, end_at)) {
    if (kind == Kind::Tone) {
      finish(AudioCode::Finished);
    } else if (++note_i < mel->count) {
      start_note(now);
    } else if (mel->repeat == 0xFF || repeats_done < mel->repeat) {
      ++repeats_done;
      note_i = 0;
      start_note(now);
    } else {
      finish(AudioCode::Finished);
    }
  }
#if CORE0_FEAT_AUDIO_STREAM
  if (kind == Kind::Stream && src) {
    int16_t buf[64];
    size_t moved = 0;
    while (!src_ended && moved < kPumpPerTick && ring.free_space() >= count_of(buf)) {
      const size_t n = src->read(buf, count_of(buf));
      if (n == 0) { src_ended = true; break; }
      for (size_t i = 0; i < n; ++i) ring.push(buf[i]);
      moved += n;
    }
    if (src_ended && ring.empty()) finish(AudioCode::Finished);
  }
  const uint32_t u = underruns;
  if (u != underruns_reported && (!underrun_reported_once || time_since(now, underrun_report_at) >= kUnderrunReportMs)) {
    underrun_reported_once = true;
    post(AudioCode::Underrun, prio, u - underruns_reported);
    underruns_reported = u;
    underrun_report_at = now;
  }
#endif
#if CORE0_FEAT_AUDIO_IN
  if (sink && in) {
    int16_t buf[128];
    for (size_t pass = 0; pass < 2; ++pass) {
      const size_t n = in->read(buf, count_of(buf));
      if (n == 0) break;
      const size_t w = sink->write(buf, n);
      if (w < n) post(AudioCode::CaptureOverrun, AudioPriority::Media, static_cast<uint32_t>(n - w));
    }
  }
#endif
  if (amp_off_pending && kind == Kind::None && time_reached(now, amp_off_at)) {
    amp_off_pending = false;
    if (out) out->set_power(false);
    amp_on = false;
  }
}

ms_t next_deadline(ms_t now) {
  ms_t best = now + kIdleDeadlineMs;
  if (kind == Kind::Tone || kind == Kind::Melody) best = end_at;
#if CORE0_FEAT_AUDIO_STREAM
  if (kind == Kind::Stream) best = now + 4;  // keep the ring topped up
#endif
#if CORE0_FEAT_AUDIO_IN
  if (sink) best = now + 4;  // DMA holds ~16 ms
#endif
  if (amp_off_pending && time_until(best, amp_off_at) < 0) best = amp_off_at;
  return best;
}

bool can_enter(PowerState target) {
  if (target < PowerState::Sleep) return true;
#if CORE0_FEAT_AUDIO_IN
  if (sink) return false;
#endif
  return kind == Kind::None;
}

void on_suspend(PowerState target) {
  if (!out) return;
  if (target >= PowerState::Sleep) {
    if (kind != Kind::None) finish(AudioCode::Preempted);
#if CORE0_FEAT_AUDIO_IN
    capture_stop();
    if (in) in->set_power(false);
#endif
  }
  if (target >= PowerState::Dim && kind == Kind::None) {
    amp_off_pending = false;
    out->set_power(false);  // drivers with a keep-warm policy decide themselves
    amp_on = false;
  }
}

}  // namespace

Status attach_out(AudioOut& o) {
  out = &o;
  return Status::Ok;
}

#if CORE0_FEAT_AUDIO_IN
Status attach_in(AudioIn& i) {
  in = &i;
  return Status::Ok;
}
#endif

Status beep(uint16_t freq_hz, uint16_t ms, AudioPriority p) {
  if (!out) return Status::Unsupported;
  if (!admit(p)) return Status::Busy;
  take_over(p);
  kind = Kind::Tone;
  out->tone(freq_hz);
  end_at = hal::millis() + ms;
  post(AudioCode::Started, p);
  return Status::Ok;
}

Status play(const Melody& m, AudioPriority p) {
  if (!out || m.count == 0) return Status::Invalid;
  if (!admit(p)) return Status::Busy;
  take_over(p);
  kind = Kind::Melody;
  mel = &m;
  note_i = 0;
  repeats_done = 0;
  start_note(hal::millis());
  post(AudioCode::Started, p);
  return Status::Ok;
}

#if CORE0_FEAT_AUDIO_STREAM
Status stream(SampleSource& s, AudioPriority p) {
  if (!out || !out->can_stream()) return Status::Unsupported;
  if (!admit(p)) return Status::Busy;
  take_over(p);
  const Status st = out->stream_start(s.sample_rate());
  if (!ok(st)) {
    finish(AudioCode::Finished);
    return st;
  }
  src = &s;
  src_ended = false;
  kind = Kind::Stream;
  post(AudioCode::Started, p);
  return Status::Ok;
}

size_t drain(int16_t* dst, size_t n) {
  size_t i = 0;
  while (i < n && ring.pop(dst[i])) ++i;
  if (i < n && kind == Kind::Stream && !src_ended) underruns = underruns + 1;
  return i;
}
#endif

void stop(AudioPriority p) {
  if (kind != Kind::None && static_cast<uint8_t>(prio) <= static_cast<uint8_t>(p)) finish(AudioCode::Finished);
}

bool busy() { return kind != Kind::None; }
AudioPriority current() { return prio; }

void set_volume(uint8_t v) {
  vol = v;
  if (out) out->set_volume(v);
}
uint8_t volume() { return vol; }
void set_muted(bool m) { muted = m; }

#if CORE0_FEAT_AUDIO_IN
Status capture_start(SampleSink& s, uint32_t rate) {
  if (!in) return Status::Unsupported;
  if (rate != in_rate) {
    const Status st = in->begin(rate);
    if (!ok(st)) return st;
    in_rate = rate;
  }
  in->set_power(true);
  const Status st = in->start();
  if (!ok(st)) return st;
  sink = &s;
  if (cap_lock == kNoLock) cap_lock = power::lock(PowerState::Idle, "capture");
  return Status::Ok;
}

void capture_stop() {
  if (!sink) return;
  if (in) in->stop();
  sink = nullptr;
  power::unlock(cap_lock);
}

bool capturing() { return sink != nullptr; }
#endif

const Module kModule = {"audio", init, tick, next_deadline, can_enter, on_suspend, nullptr};

}  // namespace audio

#if defined(CORE0_NATIVE)
namespace test {
void reset_audio() {
  audio::out = nullptr;
  audio::kind = audio::Kind::None;
  audio::prio = AudioPriority::UiClick;
  audio::vol = 255;
  audio::muted = false;
  audio::amp_on = false;
  audio::amp_off_pending = false;
  audio::play_lock = kNoLock;
#if CORE0_FEAT_AUDIO_STREAM
  int16_t s;
  while (audio::ring.pop(s)) {
  }
  audio::src = nullptr;
  audio::paused_src = nullptr;
  audio::underruns = 0;
  audio::underruns_reported = 0;
  audio::underrun_reported_once = false;
#endif
#if CORE0_FEAT_AUDIO_IN
  audio::in = nullptr;
  audio::sink = nullptr;
  audio::in_rate = 0;
  audio::cap_lock = kNoLock;
#endif
}
}  // namespace test
#endif

}  // namespace core0

#endif  // CORE0_FEAT_AUDIO
