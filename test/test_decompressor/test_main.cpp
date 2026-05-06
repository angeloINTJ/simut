/**
 * @file    test/test_decompressor/test_main.cpp
 * @brief   Fase 3 OTA — host-side tests do wrapper gunzip + uzlib vendored.
 *
 * Pull-based: caller fornece source callback; wrapper consome até EOF.
 *
 * Roda via `pio test -e native_decompressor`.
 *
 * @license MIT
 */
#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <unistd.h>
#include "../../src/ota/decompressor.h"
#include "../../src/ota/decompressor.cpp"

static ota::GunzipContext g_ctx;

/* ----- Source: vector com cursor ----- */
struct VecSource { const std::vector<uint8_t>* data; size_t off; };
static int vec_src_cb(void* user) {
    VecSource* s = static_cast<VecSource*>(user);
    if (s->off >= s->data->size()) return -1;
    return (*s->data)[s->off++];
}

/* ----- Sink: acumula em vector, opcional cap pra abort ----- */
struct Sink { std::vector<uint8_t> data; size_t cap_remaining = SIZE_MAX; };
static bool sink_cb(const uint8_t* d, size_t n, void* user) {
    Sink* s = static_cast<Sink*>(user);
    if (n > s->cap_remaining) return false;
    s->cap_remaining -= n;
    s->data.insert(s->data.end(), d, d + n);
    return true;
}

/* gzip via system gzip(1) — env:native roda em Linux. */
static std::vector<uint8_t> gzip_via_system(const std::string& plain) {
    char in_path[]  = "/tmp/uzlib_test_in_XXXXXX";
    char out_path[64];
    int fd = mkstemp(in_path);
    if (fd < 0) return {};
    write(fd, plain.data(), plain.size());
    close(fd);
    snprintf(out_path, sizeof(out_path), "%s.gz", in_path);
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "gzip -c -n %s > %s", in_path, out_path);
    if (system(cmd) != 0) { unlink(in_path); return {}; }
    FILE* f = fopen(out_path, "rb");
    if (!f) { unlink(in_path); unlink(out_path); return {}; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> out(sz);
    fread(out.data(), 1, sz, f);
    fclose(f);
    unlink(in_path); unlink(out_path);
    return out;
}

/* ---------- Tests ---------- */

void test_small_blob() {
    std::string plain = "Hello, SIMUT OTA Phase 3 — uzlib vendored.\n";
    auto gz = gzip_via_system(plain);
    TEST_ASSERT_GREATER_THAN(20, gz.size());

    VecSource src{&gz, 0};
    Sink sink;
    TEST_ASSERT_TRUE(ota::gunzip_begin(g_ctx));
    TEST_ASSERT_TRUE(ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink));
    TEST_ASSERT_TRUE(ota::gunzip_finish(g_ctx));
    TEST_ASSERT_EQUAL_UINT(plain.size(), sink.data.size());
    TEST_ASSERT_EQUAL_MEMORY(plain.data(), sink.data.data(), plain.size());
}

void test_large_blob_500k() {
    std::string plain;
    plain.reserve(512 * 1024);
    while (plain.size() < 500 * 1024) {
        char buf[80];
        snprintf(buf, sizeof(buf),
            "[line %05zu] simut sensor=%d temp=%.2f hum=%.1f ts=%lu\n",
            plain.size() / 64, (int)(plain.size() % 10),
            20.0 + (plain.size() % 50) / 10.0,
            40.0 + (plain.size() % 30),
            (unsigned long)plain.size());
        plain += buf;
    }
    auto gz = gzip_via_system(plain);
    TEST_ASSERT_LESS_THAN(plain.size() / 2, gz.size());

    VecSource src{&gz, 0};
    Sink sink;
    TEST_ASSERT_TRUE(ota::gunzip_begin(g_ctx));
    TEST_ASSERT_TRUE(ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink));
    TEST_ASSERT_TRUE(ota::gunzip_finish(g_ctx));
    TEST_ASSERT_EQUAL_UINT(plain.size(), sink.data.size());
    TEST_ASSERT_EQUAL_MEMORY(plain.data(), sink.data.data(), plain.size());
}

/* Source que conta byte-a-byte (sem buffering) — exercita pull pesado. */
void test_pull_byte_by_byte() {
    std::string plain;
    for (int i = 0; i < 200; i++) {
        char buf[80];
        snprintf(buf, sizeof(buf), "Pull-mode line %05d test variation %d\n", i, i*3);
        plain += buf;
    }
    auto gz = gzip_via_system(plain);

    VecSource src{&gz, 0};
    Sink sink;
    TEST_ASSERT_TRUE(ota::gunzip_begin(g_ctx));
    TEST_ASSERT_TRUE(ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink));
    TEST_ASSERT_TRUE(ota::gunzip_finish(g_ctx));
    TEST_ASSERT_EQUAL_UINT(plain.size(), sink.data.size());
    TEST_ASSERT_EQUAL_MEMORY(plain.data(), sink.data.data(), plain.size());
}

void test_bitflip_detected() {
    std::string plain;
    while (plain.size() < 4096) plain += "abcdefghij0123456789\n";
    auto gz = gzip_via_system(plain);
    std::vector<size_t> positions = {3, gz.size() / 2, gz.size() - 8};

    int detected = 0;
    for (size_t pos : positions) {
        auto mut = gz;
        mut[pos] ^= 0xFF;
        VecSource src{&mut, 0};
        Sink sink;
        ota::gunzip_begin(g_ctx);
        bool ok1 = ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink);
        bool ok2 = ota::gunzip_finish(g_ctx);
        if (!ok1 || !ok2) detected++;
    }
    TEST_ASSERT_EQUAL_INT(3, detected);
}

void test_callback_abort() {
    std::string plain;
    while (plain.size() < 8192) plain += "abc";
    auto gz = gzip_via_system(plain);

    VecSource src{&gz, 0};
    Sink sink;
    sink.cap_remaining = 100;  /* aceita só 100 B */
    ota::gunzip_begin(g_ctx);
    bool ok = ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink);
    bool fin = ota::gunzip_finish(g_ctx);
    TEST_ASSERT_FALSE_MESSAGE(ok, "decompress deveria reportar abort por out_cb=false");
    TEST_ASSERT_FALSE(fin);
    TEST_ASSERT_LESS_OR_EQUAL_UINT(100u + 1024u, sink.data.size());  /* até 1 chunk a mais é OK (granularidade do output) */
}

void test_truncated_input() {
    std::string plain = "Some content for truncation test.\n";
    auto gz = gzip_via_system(plain);
    /* Truncar metade */
    gz.resize(gz.size() / 2);
    VecSource src{&gz, 0};
    Sink sink;
    ota::gunzip_begin(g_ctx);
    bool ok = ota::gunzip_decompress(g_ctx, vec_src_cb, &src, sink_cb, &sink);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_FALSE(ota::gunzip_finish(g_ctx));
}

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_small_blob);
    RUN_TEST(test_large_blob_500k);
    RUN_TEST(test_pull_byte_by_byte);
    RUN_TEST(test_bitflip_detected);
    RUN_TEST(test_callback_abort);
    RUN_TEST(test_truncated_input);
    return UNITY_END();
}
