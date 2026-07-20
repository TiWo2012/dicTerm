/**
 * @file test_clipboard.c
 * @brief Unit tests for the clipboard module (base64 decoder).
 */
#include "clipboard.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { tests_run++;                           \
    printf("  test: %-50s ", name);                            \
    fflush(stdout);                                            \
} while (0)

#define PASS do { printf("ok\n"); tests_passed++; } while (0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while (0)
#define CHECK(cond, msg) do {                                  \
    if (!(cond)) { FAIL(msg); return; }                        \
} while (0)

// ---------------------------------------------------------------------------
// base64_decode tests
// ---------------------------------------------------------------------------

static void test_b64_empty(void) {
    TEST("empty string");
    uint8_t out[16];
    int n = base64_decode("", out, sizeof(out));
    CHECK(n == 0, "expected 0 bytes");
    PASS;
}

static void test_b64_hello(void) {
    TEST("decode 'hello'");
    uint8_t out[16];
    int n = base64_decode("aGVsbG8=", out, sizeof(out));
    CHECK(n == 5, "expected 5 bytes");
    CHECK(memcmp(out, "hello", 5) == 0, "expected 'hello'");
    PASS;
}

static void test_b64_world(void) {
    TEST("decode 'world'");
    uint8_t out[16];
    int n = base64_decode("d29ybGQ=", out, sizeof(out));
    CHECK(n == 5, "expected 5 bytes");
    CHECK(memcmp(out, "world", 5) == 0, "expected 'world'");
    PASS;
}

static void test_b64_hello_world(void) {
    TEST("decode 'hello world'");
    uint8_t out[32];
    int n = base64_decode("aGVsbG8gd29ybGQ=", out, sizeof(out));
    CHECK(n == 11, "expected 11 bytes");
    CHECK(memcmp(out, "hello world", 11) == 0, "expected 'hello world'");
    PASS;
}

static void test_b64_nopad(void) {
    TEST("decode without padding");
    uint8_t out[16];
    int n = base64_decode("aGVsbG8", out, sizeof(out));
    CHECK(n == 5, "expected 5 bytes");
    CHECK(memcmp(out, "hello", 5) == 0, "expected 'hello'");
    PASS;
}

static void test_b64_binary(void) {
    TEST("decode binary data");
    // base64 of bytes [0x00, 0x01, 0x02, 0xFF, 0xFE]
    uint8_t expected[] = { 0x00, 0x01, 0x02, 0xFF, 0xFE };
    uint8_t out[16];
    int n = base64_decode("AAEC//4=", out, sizeof(out));
    CHECK(n == 5, "expected 5 bytes");
    CHECK(memcmp(out, expected, 5) == 0, "expected binary data");
    PASS;
}

static void test_b64_whitespace(void) {
    TEST("decode with whitespace");
    uint8_t out[16];
    int n = base64_decode("a GVs\nbG8\t", out, sizeof(out));
    CHECK(n == 5, "expected 5 bytes");
    CHECK(memcmp(out, "hello", 5) == 0, "expected 'hello'");
    PASS;
}

static void test_b64_unicode(void) {
    TEST("decode unicode (UTF-8)");
    // "héllò" in UTF-8: h=C3A9, ll, ò=C3B2
    uint8_t out[16];
    int n = base64_decode("aMOpbGzDsg==", out, sizeof(out));
    CHECK(n == 7, "expected 7 bytes");
    CHECK(memcmp(out, "h\xC3\xA9ll\xC3\xB2", 7) == 0, "expected 'héllò'");
    PASS;
}

static void test_b64_long(void) {
    TEST("decode long string (truncated to buffer)");
    uint8_t out[10];
    // 7 A's base64 = "QUFBQUFBQUFB", buffer limited to 5 bytes
    int n = base64_decode("QUFBQUFBQUFB", out, 5);
    CHECK(n == 5, "expected 5 bytes (truncated)");
    CHECK(memcmp(out, "AAAAA", 5) == 0, "expected 5 A's");
    PASS;
}

// ---------------------------------------------------------------------------

int main(void) {
    printf("clipboard test suite\n");
    printf("====================\n\n");

    test_b64_empty();
    test_b64_hello();
    test_b64_world();
    test_b64_hello_world();
    test_b64_nopad();
    test_b64_binary();
    test_b64_whitespace();
    test_b64_unicode();
    test_b64_long();

    printf("\n================\n");
    printf("results: %d / %d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
