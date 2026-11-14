// T042 — settings service: A/B slots, power-cut safety, deferred commit, migration
// (settings.md acceptance; constitution I commit exception).
#include <unity.h>
#include "test_helpers.h"
using namespace core0;

namespace core0 {
namespace test {
void reset_settings();
}
}  // namespace core0

struct S1 {
  int16_t a, b, c, d, e, f, g, h, i, j, k, l;  // 24 bytes
};
struct S2 {
  int16_t a, b, c, d, e, f, g, h, i, j, k, l;
  int16_t added;  // v2 appends a field
};
static void defaults1(S1& s) { memset(&s, 0, sizeof(s)); s.a = 1; s.l = 12; }
static void defaults2(S2& s) { memset(&s, 0, sizeof(s)); s.a = 1; s.l = 12; s.added = 77; }

static Settings<S1, 1> g1(defaults1);
static Settings<S2, 2> g2(defaults2);

static void boot1() {
  test::reset_settings();
  g1.bind();
  settings::kModule.init();
}
static void boot2() {
  test::reset_settings();
  g2.bind();
  settings::kModule.init();
}
static bool equals(const S1& x, int16_t base) {
  const int16_t* p = &x.a;
  for (int i = 0; i < 12; ++i)
    if (p[i] != base + i) return false;
  return true;
}
static void fill(S1& x, int16_t base) {
  int16_t* p = &x.a;
  for (int i = 0; i < 12; ++i) p[i] = static_cast<int16_t>(base + i);
}

void setUp() {
  reset_all();
  test::reset_settings();
  rec_attach(EventType::Settings);
}
void tearDown() {}

static void test_empty_nvm_defaults() {
  boot1();
  TEST_ASSERT_EQUAL_INT16(1, g1->a);
  TEST_ASSERT_EQUAL_INT16(12, g1->l);
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Settings, U(SettingsCode::Defaulted)));
}

static void test_save_then_reload() {
  boot1();
  fill(*g1, 100);
  settings::save_now();
  pump(2);
  memset(&g1.value, 0, sizeof(S1));
  boot1();
  TEST_ASSERT_TRUE(equals(*g1, 100));
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Settings, U(SettingsCode::Loaded)));
}

static void test_power_cut_at_every_byte_gives_old_or_new() {
  boot1();
  fill(*g1, 100);  // "old"
  settings::save_now();
  pump(2);
  uint8_t snapshot[256];
  memcpy(snapshot, port_native::nvm_raw(), sizeof(snapshot));
  const int bytes_per_save = static_cast<int>(sizeof(SettingsHeader) + sizeof(S1));
  int saw_old = 0, saw_new = 0;
  for (int n = 0; n <= bytes_per_save; ++n) {
    memcpy(port_native::nvm_raw(), snapshot, sizeof(snapshot));
    boot1();
    TEST_ASSERT_TRUE(equals(*g1, 100));
    fill(*g1, 200);  // "new"
    port_native::nvm_fail_after(n);
    settings::save_now();
    pump(2);
    port_native::nvm_fail_after(-1);  // power restored
    memset(&g1.value, 0, sizeof(S1));
    boot1();
    const bool old_v = equals(*g1, 100), new_v = equals(*g1, 200);
    TEST_ASSERT_TRUE_MESSAGE(old_v || new_v, "garbage or defaults after power cut");
    saw_old += old_v;
    saw_new += new_v;
  }
  TEST_ASSERT_TRUE(saw_old > 0);
  TEST_ASSERT_TRUE(saw_new > 0);
}

static void test_ten_mark_dirty_one_write_after_delay() {
  boot1();
  const uint32_t c0 = port_native::nvm_commit_count();
  for (int i = 0; i < 10; ++i) {
    g1->b = static_cast<int16_t>(i);
    g1.changed();
    pump(200);
  }
  TEST_ASSERT_EQUAL_UINT32(c0, port_native::nvm_commit_count());
  pump(2700);  // 2900 ms after the last change
  TEST_ASSERT_EQUAL_UINT32(c0, port_native::nvm_commit_count());
  pump(200);   // 3100 ms
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
  pump(5000);
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
}

static void test_migration_appended_field() {
  boot1();
  fill(*g1, 300);
  settings::save_now();
  pump(2);
  g_rec.n = 0;
  boot2();
  const int16_t* p = &g2->a;
  for (int i = 0; i < 12; ++i) TEST_ASSERT_EQUAL_INT16(300 + i, p[i]);
  TEST_ASSERT_EQUAL_INT16(77, g2->added);
  bus::dispatch();
  const int i = rec_find(EventType::Settings, U(SettingsCode::Migrated));
  TEST_ASSERT_TRUE(i >= 0);
  TEST_ASSERT_EQUAL_UINT32((1u << 16) | 2u, g_rec.ev[i].data);
}

static void test_corrupt_crc_defaults() {
  boot1();
  fill(*g1, 100);
  settings::save_now();
  pump(2);
  port_native::nvm_raw()[sizeof(SettingsHeader) + 3] ^= 0xFF;  // corrupt slot A payload
  g_rec.n = 0;
  boot1();
  TEST_ASSERT_EQUAL_INT16(1, g1->a);
  bus::dispatch();
  TEST_ASSERT_EQUAL_INT(1, rec_count(EventType::Settings, U(SettingsCode::Defaulted)));
}

static void test_factory_reset_commits_next_pass() {
  boot1();
  fill(*g1, 100);
  settings::save_now();
  pump(2);
  const uint32_t c0 = port_native::nvm_commit_count();
  TEST_ASSERT_EQUAL(U(Status::Ok), U(settings::factory_reset()));
  TEST_ASSERT_EQUAL_INT16(1, g1->a);                           // defaults applied immediately
  TEST_ASSERT_EQUAL_UINT32(c0, port_native::nvm_commit_count());  // not written inside the call
  pump(1);
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
  boot1();
  TEST_ASSERT_EQUAL_INT16(1, g1->a);
  TEST_ASSERT_EQUAL_INT16(0, g1->b);
}

static void test_save_now_commits_next_pass() {
  boot1();
  g1->c = 5;
  const uint32_t c0 = port_native::nvm_commit_count();
  settings::save_now();
  TEST_ASSERT_EQUAL_UINT32(c0, port_native::nvm_commit_count());
  pump(1);
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
  TEST_ASSERT_FALSE(settings::dirty());
}

static void test_on_suspend_flushes_dirty() {
  boot1();
  g1->d = 9;
  g1.changed();
  const uint32_t c0 = port_native::nvm_commit_count();
  settings::kModule.on_suspend(PowerState::Sleep);
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
  TEST_ASSERT_FALSE(settings::dirty());
  settings::kModule.on_suspend(PowerState::Sleep);  // clean: no second write
  TEST_ASSERT_EQUAL_UINT32(c0 + 1, port_native::nvm_commit_count());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_nvm_defaults);
  RUN_TEST(test_save_then_reload);
  RUN_TEST(test_power_cut_at_every_byte_gives_old_or_new);
  RUN_TEST(test_ten_mark_dirty_one_write_after_delay);
  RUN_TEST(test_migration_appended_field);
  RUN_TEST(test_corrupt_crc_defaults);
  RUN_TEST(test_factory_reset_commits_next_pass);
  RUN_TEST(test_save_now_commits_next_pass);
  RUN_TEST(test_on_suspend_flushes_dirty);
  return UNITY_END();
}
