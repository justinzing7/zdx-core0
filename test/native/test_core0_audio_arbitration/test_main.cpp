// T052 — audio service: priority arbitration, mute, melody, locks, amp power-off, stream ring
// (audio.md "Arbitration", "Streaming"; acceptance 1, 4).
#include <unity.h>
#include "test_helpers.h"
#include "mock_audio.h"
using namespace core0;

namespace core0 {
namespace test {
void reset_audio();
}
}  // namespace core0

static MockAudioOut g_out;

static int lock_count() {
  // Count "audio" locks by probing: a lock taken now gets the next free slot index.
  PowerLock l = power::lock(PowerState::Active, "probe");
  const int idx = l;
  power::unlock(l);
  return idx;
}

void setUp() {
  reset_all();
  test::reset_audio();
  g_out.reset();
  audio::attach_out(g_out);
  sys::register_module(&audio::kModule);
  rec_attach(EventType::Audio);
  sys::begin();
  g_out.reset();
}
void tearDown() {}

static void test_ui_click_plays_and_finishes() {
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::beep(4000, 6, AudioPriority::UiClick)));
  TEST_ASSERT_EQUAL_UINT16(4000, g_out.freq);  // started immediately, same pass
  TEST_ASSERT_TRUE(g_out.powered);
  TEST_ASSERT_TRUE(audio::busy());
  pump(7);
  TEST_ASSERT_EQUAL_UINT16(0, g_out.freq);
  TEST_ASSERT_FALSE(audio::busy());
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Audio, U(AudioCode::Finished)));
}

static void test_click_replaced_by_anything() {
  audio::beep(4000, 50, AudioPriority::UiClick);
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::beep(1000, 50, AudioPriority::Notify)));
  TEST_ASSERT_EQUAL_UINT16(1000, g_out.freq);
  TEST_ASSERT_EQUAL(U(AudioPriority::Notify), U(audio::current()));
}

static void test_notify_drops_click_and_is_preempted_by_notify() {
  audio::beep(1000, 50, AudioPriority::Notify);
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(4000, 6, AudioPriority::UiClick)));
  TEST_ASSERT_EQUAL_UINT16(1000, g_out.freq);
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::beep(1500, 50, AudioPriority::Notify)));
  TEST_ASSERT_EQUAL_UINT16(1500, g_out.freq);
  pump(1);
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Audio, U(AudioCode::Preempted)));
}

static void test_alarm_drops_everything_but_alarm() {
  audio::beep(800, 100, AudioPriority::Alarm);
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(4000, 6, AudioPriority::UiClick)));
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(1000, 50, AudioPriority::Notify)));
  TEST_ASSERT_EQUAL_UINT16(800, g_out.freq);
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::beep(900, 50, AudioPriority::Alarm)));
  TEST_ASSERT_EQUAL_UINT16(900, g_out.freq);
}

class CountSource : public SampleSource {
 public:
  size_t produced = 0, limit = 1u << 30;
  uint32_t sample_rate() const override { return 16000; }
  size_t read(int16_t* dst, size_t n) override {
    if (produced >= limit) return 0;
    for (size_t i = 0; i < n; ++i) dst[i] = static_cast<int16_t>(produced + i);
    produced += n;
    return n;
  }
};

static void test_media_drops_notify_and_alarm_pauses_then_resumes_media() {
  CountSource src;
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::stream(src, AudioPriority::Media)));
  TEST_ASSERT_TRUE(g_out.streaming);
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(1000, 50, AudioPriority::Notify)));
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(4000, 6, AudioPriority::UiClick)));
  audio::beep(800, 30, AudioPriority::Alarm);
  TEST_ASSERT_FALSE(g_out.streaming);  // media paused
  TEST_ASSERT_EQUAL(U(AudioPriority::Alarm), U(audio::current()));
  const size_t pos = src.produced;
  pump(31);
  TEST_ASSERT_TRUE(g_out.streaming);   // media resumed after the alarm
  TEST_ASSERT_EQUAL(U(AudioPriority::Media), U(audio::current()));
  int16_t buf[16];
  audio::drain(buf, 16);
  TEST_ASSERT_TRUE(src.produced >= pos);  // source position kept (continues, not restarted)
}

static void test_mute_silences_click_and_notify_not_alarm() {
  audio::set_muted(true);
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(4000, 6, AudioPriority::UiClick)));
  TEST_ASSERT_EQUAL(U(Status::Busy), U(audio::beep(1000, 6, AudioPriority::Notify)));
  TEST_ASSERT_EQUAL_UINT16(0, g_out.freq);
  TEST_ASSERT_EQUAL(U(Status::Ok), U(audio::beep(800, 6, AudioPriority::Alarm)));
  TEST_ASSERT_EQUAL_UINT16(800, g_out.freq);
}

static const Note kNotes[] = {{880, 10}, {0, 5}, {660, 10}};
static const Melody kMel = {kNotes, 3, 1};  // play twice

static void test_melody_sequence_and_repeat() {
  audio::play(kMel, AudioPriority::Notify);
  uint16_t seen[16];
  int n = 0;
  uint16_t last = 0xFFFF;
  for (int t = 0; t < 80 && n < 16; ++t) {
    if (g_out.freq != last) { last = g_out.freq; seen[n++] = last; }
    pump(1);
  }
  // 880, rest, 660, then again, then silence
  TEST_ASSERT_TRUE(n >= 6);
  TEST_ASSERT_EQUAL_UINT16(880, seen[0]);
  TEST_ASSERT_EQUAL_UINT16(0, seen[1]);
  TEST_ASSERT_EQUAL_UINT16(660, seen[2]);
  TEST_ASSERT_EQUAL_UINT16(880, seen[3]);
  TEST_ASSERT_FALSE(audio::busy());
}

static void test_power_lock_held_while_playing() {
  const int idle_slots = lock_count();
  audio::beep(1000, 20, AudioPriority::Notify);
  TEST_ASSERT_EQUAL_INT(idle_slots + 1, lock_count());
  pump(21);
  TEST_ASSERT_EQUAL_INT(idle_slots, lock_count());
}

static void test_amp_off_200ms_after_playback() {
  audio::beep(1000, 10, AudioPriority::Notify);
  pump(11);
  TEST_ASSERT_TRUE(g_out.powered);
  pump(195);
  TEST_ASSERT_TRUE(g_out.powered);
  pump(10);
  TEST_ASSERT_FALSE(g_out.powered);
}

static void test_stream_ring_drain_and_underrun() {
  CountSource src;
  src.limit = 64;  // short stream
  audio::stream(src, AudioPriority::Media);
  pump(2);
  int16_t buf[128];
  const size_t got = audio::drain(buf, 128);
  TEST_ASSERT_EQUAL_UINT(64, got);
  TEST_ASSERT_EQUAL_INT16(0, buf[0]);
  TEST_ASSERT_EQUAL_INT16(63, buf[63]);
  pump(5);
  TEST_ASSERT_FALSE(audio::busy());  // source ended + ring drained -> finished
  CountSource forever;
  audio::stream(forever, AudioPriority::Media);
  pump(1);
  // Drain far more than produced -> underrun reported
  for (int i = 0; i < 64; ++i) audio::drain(buf, 128);
  pump(2);
  TEST_ASSERT_TRUE(rec_count(EventType::Audio, U(AudioCode::Underrun)) >= 1);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_ui_click_plays_and_finishes);
  RUN_TEST(test_click_replaced_by_anything);
  RUN_TEST(test_notify_drops_click_and_is_preempted_by_notify);
  RUN_TEST(test_alarm_drops_everything_but_alarm);
  RUN_TEST(test_media_drops_notify_and_alarm_pauses_then_resumes_media);
  RUN_TEST(test_mute_silences_click_and_notify_not_alarm);
  RUN_TEST(test_melody_sequence_and_repeat);
  RUN_TEST(test_power_lock_held_while_playing);
  RUN_TEST(test_amp_off_200ms_after_playback);
  RUN_TEST(test_stream_ring_drain_and_underrun);
  return UNITY_END();
}
