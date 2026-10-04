/* The RP2350's first boot of an update, on trial (docs/analysis/OTA_AB_RP2350.md,
 * step 5), on the host: when the image buys itself or goes back, how the image
 * that comes back reads the version that did not stay, and how that version
 * fits the log's 16-bit context. */
#include <unity.h>
#include <string.h>
#include <vector>
#include "ota/picobin.h"
#include "ota/slot_stage.h"
#include "ota/trial.h"

using namespace ota;

/* ---- buy or go back ------------------------------------------------------ */

static TrialClock on_trial( ) {
	TrialClock c;
	memset(&c, 0, sizeof(c));
	c.pending = true;
	return c;
}

static void test_a_boot_not_on_trial_does_nothing(void) {
	TrialClock c;
	memset(&c, 0, sizeof(c));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 1000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 120000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, false, TRIAL_DEADLINE_MS + 1));
}

static void test_a_minute_of_health_buys(void) {
	TrialClock c = on_trial( );
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, false, 5000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 20000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 20000 + TRIAL_HEALTHY_MS - 1));
	TEST_ASSERT_EQUAL(TrialAction::BUY, trial_step(c, true, 20000 + TRIAL_HEALTHY_MS));
}

static void test_the_minute_counts_from_the_first_healthy_look(void) {
	TrialClock c = on_trial( );
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 7000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 66999));
	TEST_ASSERT_EQUAL(TrialAction::BUY, trial_step(c, true, 67000));
}

static void test_a_break_in_health_starts_the_minute_over(void) {
	TrialClock c = on_trial( );
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 20000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 79000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, false, 79500));   /* the Wi-Fi dropped */
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 81000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, 81000 + TRIAL_HEALTHY_MS - 1));
	TEST_ASSERT_EQUAL(TrialAction::BUY, trial_step(c, true, 81000 + TRIAL_HEALTHY_MS));
}

static void test_never_healthy_goes_back_at_the_deadline(void) {
	TrialClock c = on_trial( );
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, false, 1000));
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, false, TRIAL_DEADLINE_MS - 1));
	TEST_ASSERT_EQUAL(TrialAction::REVERT, trial_step(c, false, TRIAL_DEADLINE_MS));
	TEST_ASSERT_EQUAL(TrialAction::REVERT, trial_step(c, false, TRIAL_DEADLINE_MS + 5000));
}

static void test_health_has_to_begin_a_minute_before_the_deadline(void) {
	/* The minute is whole or it does not count: a run that would end past the
	 * deadline does not buy at the deadline. */
	TrialClock c = on_trial( );
	const uint32_t last = TRIAL_DEADLINE_MS - TRIAL_HEALTHY_MS;
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(c, true, last));
	TEST_ASSERT_EQUAL(TrialAction::BUY, trial_step(c, true, TRIAL_DEADLINE_MS));

	TrialClock late = on_trial( );
	TEST_ASSERT_EQUAL(TrialAction::NONE, trial_step(late, true, last + 1));
	TEST_ASSERT_EQUAL(TrialAction::REVERT, trial_step(late, true, TRIAL_DEADLINE_MS));
}

static void test_the_minute_and_the_deadline_are_the_decided_ones(void) {
	/* Decided on 2026-10-03: the buy after 60 s of health. The deadline leaves
	 * the Wi-Fi four minutes to come back before the minute has to start. */
	TEST_ASSERT_EQUAL_UINT32(60000, TRIAL_HEALTHY_MS);
	TEST_ASSERT_EQUAL_UINT32(300000, TRIAL_DEADLINE_MS);
}

/* ---- the image on trial, and the one the ROM went back from --------------- */

/* The first block of pico2_w_release (test_slot.cpp), at 0x124 of a 4 KB sector. */
static const uint8_t kBlock[20] = {
	0xd3, 0xde, 0xff, 0xff, 0x42, 0x01, 0x21, 0x10, 0xff, 0x01, 0x00, 0x00,
	0xf0, 0x14, 0x0f, 0x00, 0x79, 0x35, 0x12, 0xab };

static std::vector<uint8_t> first_sector(bool trial) {
	std::vector<uint8_t> v(4096, 0x00);
	memcpy(&v[0x124], kBlock, sizeof(kBlock));
	if (trial) v[0x124 + 7] |= 0x80;   /* IMAGE_TYPE 0x1021 -> 0x9021 */
	return v;
}

static void test_the_trial_bit_marks_a_trial(void) {
	std::vector<uint8_t> plain = first_sector(false), trial = first_sector(true);
	PicobinBlock b = picobin_first_block(plain.data( ), (uint32_t)plain.size( ));
	TEST_ASSERT_TRUE(picobin_is_rp2350_arm_exe(b));
	TEST_ASSERT_FALSE(picobin_is_trial(b));
	b = picobin_first_block(trial.data( ), (uint32_t)trial.size( ));
	TEST_ASSERT_TRUE(picobin_is_rp2350_arm_exe(b));
	TEST_ASSERT_TRUE(picobin_is_trial(b));
}

static void test_no_image_definition_is_no_trial(void) {
	/* An erased first sector, the state the buy and the report leave behind. */
	std::vector<uint8_t> erased(4096, 0xFF);
	PicobinBlock b = picobin_first_block(erased.data( ), (uint32_t)erased.size( ));
	TEST_ASSERT_FALSE(b.found);
	TEST_ASSERT_FALSE(picobin_is_trial(b));
}

static void test_the_running_slot_is_the_one_booted(void) {
	const uint32_t A = 0x002000, B = 0x181000;
	TEST_ASSERT_EQUAL_HEX32(A, slot_active_offset(0, A, B));
	TEST_ASSERT_EQUAL_HEX32(B, slot_active_offset(1, A, B));
	TEST_ASSERT_EQUAL_HEX32(SLOT_NONE, slot_active_offset(-1, A, B));
	TEST_ASSERT_EQUAL_HEX32(SLOT_NONE, slot_active_offset(2, A, B));
	TEST_ASSERT_EQUAL_HEX32(SLOT_NONE, slot_active_offset(-3, A, B));
}

/* ---- the version of an image, from its tag --------------------------------- */

static std::vector<uint8_t> bytes_with(const char* s, uint32_t at, uint32_t len = 4096) {
	std::vector<uint8_t> v(len);
	for (uint32_t i = 0; i < len; i++) v[i] = (uint8_t)(i * 13u + 5u);
	memcpy(&v[at], s, strlen(s) + 1);
	return v;
}

static void test_the_version_comes_from_the_tag(void) {
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:releasetwo;v=2.10.9;", 1000);
	char ver[16];
	TEST_ASSERT_TRUE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("2.10.9", ver);
}

static void test_the_bare_prefix_is_not_a_tag(void) {
	/* The scanner's own "SIMUT-ENV:" literal sits in every image, ended by a NUL. */
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:", 100);
	memcpy(&img[2000], "SIMUT-ENV:releasetwo;v=2.11.0;", 31);
	char ver[16];
	TEST_ASSERT_TRUE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("2.11.0", ver);
}

static void test_a_tag_without_a_version_is_none(void) {
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:release;", 100);
	char ver[16] = "x";
	TEST_ASSERT_FALSE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("", ver);
}

static void test_a_tag_needs_its_env_and_its_end(void) {
	char ver[16];
	std::vector<uint8_t> a = bytes_with("SIMUT-ENV:Release;v=2.10.9;", 100);   /* the env is [a-z] */
	TEST_ASSERT_FALSE(trial_tag_version(a.data( ), (uint32_t)a.size( ), ver, sizeof(ver)));
	std::vector<uint8_t> b = bytes_with("SIMUT-ENV:;v=2.10.9;", 100);
	TEST_ASSERT_FALSE(trial_tag_version(b.data( ), (uint32_t)b.size( ), ver, sizeof(ver)));
	std::vector<uint8_t> c(64, 'x');
	memcpy(&c[30], "SIMUT-ENV:releasetwo;v=2.10.9", 29);   /* runs into the end of the bytes */
	TEST_ASSERT_FALSE(trial_tag_version(c.data( ), (uint32_t)c.size( ), ver, sizeof(ver)));
	std::vector<uint8_t> d = bytes_with("SIMUT-ENV:releasetwo;v=2.10 9;", 100);
	TEST_ASSERT_FALSE(trial_tag_version(d.data( ), (uint32_t)d.size( ), ver, sizeof(ver)));
}

static void test_a_version_with_a_suffix_is_read_whole(void) {
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:releasetwo;v=2.11.0-rc1;", 300);
	char ver[16];
	TEST_ASSERT_TRUE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("2.11.0-rc1", ver);
}

static void test_a_version_too_long_for_the_buffer_is_none(void) {
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:releasetwo;v=2.10.9;", 100);
	char ver[6];   /* "2.10.9" needs 7 */
	TEST_ASSERT_FALSE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("", ver);
}

static void test_the_first_tag_is_the_image_s(void) {
	/* A slot is written from its start, and the stage erases only what the new
	 * image covers: an older image's tag can survive past the new one's end. */
	std::vector<uint8_t> img = bytes_with("SIMUT-ENV:releasetwo;v=2.11.0;", 900, 8192);
	memcpy(&img[7000], "SIMUT-ENV:releasetwo;v=2.10.0;", 31);
	char ver[16];
	TEST_ASSERT_TRUE(trial_tag_version(img.data( ), (uint32_t)img.size( ), ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("2.11.0", ver);
}

static void test_no_bytes_is_no_version(void) {
	char ver[16] = "x";
	TEST_ASSERT_FALSE(trial_tag_version(nullptr, 4096, ver, sizeof(ver)));
	TEST_ASSERT_EQUAL_STRING("", ver);
	TEST_ASSERT_FALSE(trial_tag_version((const uint8_t*)"SIMUT-ENV:a;v=1.0.0;", 20, nullptr, 16));
}

/* ---- the version in the log's context -------------------------------------- */

static void test_a_version_fits_the_log_context_as_digits(void) {
	/* The binary log keeps a code and a signed 16-bit context, not the text:
	 * major * 10000 + minor * 100 + patch, so 2.10.9 reads as 21009. */
	TEST_ASSERT_EQUAL_INT(21009, trial_version_code("2.10.9"));
	TEST_ASSERT_EQUAL_INT(21100, trial_version_code("2.11.0"));
	TEST_ASSERT_EQUAL_INT(32767, trial_version_code("3.27.67"));
	TEST_ASSERT_EQUAL_INT(909, trial_version_code("0.9.9"));
}

static void test_a_version_that_does_not_fit_is_zero(void) {
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("3.27.68"));     /* past 32767 */
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("2.100.0"));     /* minor past 99 */
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("2.10.100"));
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("2.10"));
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("2.10.0-rc1"));
	TEST_ASSERT_EQUAL_INT(0, trial_version_code("2..0"));
	TEST_ASSERT_EQUAL_INT(0, trial_version_code(""));
	TEST_ASSERT_EQUAL_INT(0, trial_version_code(nullptr));
}

void run_trial_tests(void) {
	RUN_TEST(test_a_boot_not_on_trial_does_nothing);
	RUN_TEST(test_a_minute_of_health_buys);
	RUN_TEST(test_the_minute_counts_from_the_first_healthy_look);
	RUN_TEST(test_a_break_in_health_starts_the_minute_over);
	RUN_TEST(test_never_healthy_goes_back_at_the_deadline);
	RUN_TEST(test_health_has_to_begin_a_minute_before_the_deadline);
	RUN_TEST(test_the_minute_and_the_deadline_are_the_decided_ones);
	RUN_TEST(test_the_trial_bit_marks_a_trial);
	RUN_TEST(test_no_image_definition_is_no_trial);
	RUN_TEST(test_the_running_slot_is_the_one_booted);
	RUN_TEST(test_the_version_comes_from_the_tag);
	RUN_TEST(test_the_bare_prefix_is_not_a_tag);
	RUN_TEST(test_a_tag_without_a_version_is_none);
	RUN_TEST(test_a_tag_needs_its_env_and_its_end);
	RUN_TEST(test_a_version_with_a_suffix_is_read_whole);
	RUN_TEST(test_a_version_too_long_for_the_buffer_is_none);
	RUN_TEST(test_the_first_tag_is_the_image_s);
	RUN_TEST(test_no_bytes_is_no_version);
	RUN_TEST(test_a_version_fits_the_log_context_as_digits);
	RUN_TEST(test_a_version_that_does_not_fit_is_zero);
}
