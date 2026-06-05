/**
 * @file test_charmap.c
 * @brief Unit tests for charmap.c - Switchy_BuildCharMaps and Switchy_ConvertString.
 *
 * Build (no -mwindows; console output needed):
 *   gcc charmap.c tests/test_charmap.c -o tests/test_charmap.exe -O0 -std=c99 -Wall -luser32 -lkernel32
 * Run:
 *   tests/test_charmap.exe
 */

#include "../charmap.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static int s_passed  = 0;
static int s_failed  = 0;
static int s_skipped = 0;

#define ASSERT(cond, msg)                                                         \
  do {                                                                            \
    if (cond) {                                                                   \
      s_passed++;                                                                 \
    } else {                                                                      \
      s_failed++;                                                                 \
      printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, msg);                      \
    }                                                                             \
  } while (0)

#define ASSERT_EQ(a, b, msg)    ASSERT((a) == (b), msg)
#define ASSERT_NEQ(a, b, msg)   ASSERT((a) != (b), msg)
#define ASSERT_WSTREQ(a, b, msg) ASSERT(wcscmp((a), (b)) == 0, msg)

#define SKIP(msg)                                                                 \
  do { s_skipped++; printf("SKIP  %s\n", msg); } while (0)

static void test_convert_null_hkls(void)
{
  /* Unknown HKL pair -> identity copy */
  Switchy_BuildCharMaps(NULL, NULL);
  WCHAR out[32] = {0};
  Switchy_ConvertString(L"hello", 5, out, 32, NULL, NULL);
  ASSERT_WSTREQ(out, L"hello", "NULL HKL pair: identity copy");
}

static void test_convert_empty_string(void)
{
  Switchy_BuildCharMaps(NULL, NULL);
  WCHAR out[8] = {0x1234, 0};  /* pre-fill to detect stale write */
  Switchy_ConvertString(L"", 0, out, 8, NULL, NULL);
  ASSERT_EQ(out[0], 0, "Empty string: out[0] is NUL");
}

static void test_convert_zero_outmax(void)
{
  /* outMax==0 must not write anything and not crash */
  Switchy_BuildCharMaps(NULL, NULL);
  WCHAR out[4] = {0xBEEF, 0xBEEF, 0xBEEF, 0};
  Switchy_ConvertString(L"abc", 3, out, 0, NULL, NULL);
  ASSERT_EQ(out[0], 0xBEEF, "outMax=0: output buffer untouched");
}

static void test_convert_null_out(void)
{
  /* NULL output buffer must not crash */
  Switchy_BuildCharMaps(NULL, NULL);
  Switchy_ConvertString(L"abc", 3, NULL, 16, NULL, NULL);
  ASSERT(1, "NULL out buffer: no crash");
}

static void test_convert_unknown_pair_identity(void)
{
  /* With a non-NULL but unregistered HKL pair, output == input */
  Switchy_BuildCharMaps(NULL, NULL);
  HKL fake1 = (HKL)(size_t)0xDEAD0001;
  HKL fake2 = (HKL)(size_t)0xDEAD0002;
  WCHAR out[16] = {0};
  Switchy_ConvertString(L"Test123", 7, out, 16, fake1, fake2);
  ASSERT_WSTREQ(out, L"Test123", "Unknown HKL pair: identity copy");
}

static void test_convert_equal_hkls_noop(void)
{
  /* Equal HKLs -> BuildCharMaps is a no-op; ConvertString should return identity */
  HKL h = (HKL)(size_t)0xCAFE;
  Switchy_BuildCharMaps(h, h);
  WCHAR out[16] = {0};
  Switchy_ConvertString(L"equal", 5, out, 16, h, h);
  /* maps were cleared (no-op build), so unknown pair -> identity */
  ASSERT_WSTREQ(out, L"equal", "Equal HKLs: identity copy");
}

static void test_convert_truncation(void)
{
  /* outMax smaller than inLen: output truncated, always NUL-terminated */
  Switchy_BuildCharMaps(NULL, NULL);
  WCHAR out[4] = {0};
  Switchy_ConvertString(L"abcdef", 6, out, 4, NULL, NULL);
  ASSERT_EQ(out[3], 0, "Truncated: last char is NUL");
  /* First 3 chars should be identity (unknown pair) */
  ASSERT_EQ(out[0], L'a', "Truncated: first char correct");
}

static void test_layout_specific(HKL hEN, HKL hRU)
{
  Switchy_BuildCharMaps(hEN, hRU);
  WCHAR out[64] = {0};

  /* EN -> RU specific character mappings */
  Switchy_ConvertString(L",", 1, out, 64, hEN, hRU);
  ASSERT_WSTREQ(out, L"б", "EN ',' -> RU 'б'");

  Switchy_ConvertString(L".", 1, out, 64, hEN, hRU);
  ASSERT_WSTREQ(out, L"ю", "EN '.' -> RU 'ю'");

  Switchy_ConvertString(L"`", 1, out, 64, hEN, hRU);
  ASSERT_WSTREQ(out, L"ё", "EN '`' -> RU 'ё'");

  Switchy_ConvertString(L"z", 1, out, 64, hEN, hRU);
  ASSERT_WSTREQ(out, L"я", "EN 'z' -> RU 'я'");

  Switchy_ConvertString(L"q", 1, out, 64, hEN, hRU);
  ASSERT_WSTREQ(out, L"й", "EN 'q' -> RU 'й'");

  /* RU -> EN specific character mappings */
  Switchy_ConvertString(L"б", 1, out, 64, hRU, hEN);
  ASSERT_WSTREQ(out, L",", "RU 'б' -> EN ','");

  Switchy_ConvertString(L"ю", 1, out, 64, hRU, hEN);
  ASSERT_WSTREQ(out, L".", "RU 'ю' -> EN '.'");

  Switchy_ConvertString(L"я", 1, out, 64, hRU, hEN);
  ASSERT_WSTREQ(out, L"z", "RU 'я' -> EN 'z'");

  /* Bug-3 regression: ASCII punctuation must pass through in RU->EN */
  Switchy_ConvertString(L".", 1, out, 64, hRU, hEN);
  ASSERT_WSTREQ(out, L".", "RU->EN: '.' passes through (not '/')");

  Switchy_ConvertString(L",", 1, out, 64, hRU, hEN);
  ASSERT_WSTREQ(out, L",", "RU->EN: ',' passes through (not '?')");

  /* Round-trip: convert Cyrillic EN->RU->EN */
  const WCHAR *cyrillic = L"привет"; /* привет */
  WCHAR mid[16] = {0};
  WCHAR back[16] = {0};
  Switchy_ConvertString(cyrillic, wcslen(cyrillic), mid, 16, hRU, hEN);
  Switchy_ConvertString(mid, wcslen(mid), back, 16, hEN, hRU);
  ASSERT_WSTREQ(back, cyrillic, "Round-trip RU->EN->RU: привет");

  /* Mixed text: Cyrillic + ASCII punctuation RU->EN */
  /* "привет," - comma should survive as ',' not '?' */
  const WCHAR mixed[] = {0x043f, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442, L',', 0};
  Switchy_ConvertString(mixed, wcslen(mixed), out, 64, hRU, hEN);
  ASSERT_EQ(out[wcslen(out) - 1], L',', "Mixed RU->EN: trailing comma preserved");
}

int main(void)
{
  printf("=== Switchy charmap tests ===\n\n");

  /* Layout-independent tests */
  test_convert_null_hkls();
  test_convert_empty_string();
  test_convert_zero_outmax();
  test_convert_null_out();
  test_convert_unknown_pair_identity();
  test_convert_equal_hkls_noop();
  test_convert_truncation();

  /* Layout-dependent tests: require EN (0409) and RU (0419) */
  HKL hEN = LoadKeyboardLayoutW(L"00000409", KLF_NOTELLSHELL);
  HKL hRU = LoadKeyboardLayoutW(L"00000419", KLF_NOTELLSHELL);

  if (!hEN || !hRU)
  {
    SKIP("EN or RU keyboard layout not installed - skipping layout-specific tests");
    if (!hEN) SKIP("  missing: 00000409 (English US)");
    if (!hRU) SKIP("  missing: 00000419 (Russian)");
  }
  else
  {
    test_layout_specific(hEN, hRU);
  }

  printf("\n=== Results: %d passed, %d failed, %d skipped ===\n",
         s_passed, s_failed, s_skipped);
  return s_failed > 0 ? 1 : 0;
}
