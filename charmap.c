/**
 * @file charmap.c
 * @brief Implementation of per-code-point maps between two keyboard layouts.
 */

#include "charmap.h"
#include <string.h>

#ifndef TO_UNICODE_NO_KEYBOARD_STATE_MUTATION
#define TO_UNICODE_NO_KEYBOARD_STATE_MUTATION 0x4 ///< ToUnicodeEx: do not mutate global keyboard state (Win10 1607+).
#endif

static WCHAR g_map_l1_to_l2[65536]; ///< L1->L2: UTF-16 index -> mapped char; 0 means no mapping stored.
static WCHAR g_map_l2_to_l1[65536]; ///< L2->L1 reverse map.

static HKL g_h1; ///< Layout handle from last Switchy_BuildCharMaps (first).
static HKL g_h2; ///< Layout handle from last Switchy_BuildCharMaps (second).

/**
 * @brief Zeros both direction maps.
 */
static void ClearMaps(void)
{
  memset(g_map_l1_to_l2, 0, sizeof(g_map_l1_to_l2));
  memset(g_map_l2_to_l1, 0, sizeof(g_map_l2_to_l1));
}

/**
 * @brief Zeros a 256-byte keyboard state array.
 */
static void SetKeyStateEmpty(BYTE ks[256])
{
  memset(ks, 0, 256);
}

/**
 * @brief Sets modifier bits in a keyboard state for one of six fixed combinations (Shift / AltGr / ...).
 * @param ks Output keyboard state.
 * @param stateIdx 0=none, 1=Shift, 2=Ctrl, 3=Shift+Ctrl, 4=Alt, 5=Ctrl+Alt (AltGr-style).
 */
static void ApplyStateIndex(BYTE ks[256], int stateIdx)
{
  SetKeyStateEmpty(ks);
  switch (stateIdx)
  {
  case 0:
    break;
  case 1:
    ks[VK_SHIFT] = 0x80;
    break;
  case 2:
    ks[VK_CONTROL] = 0x80;
    break;
  case 3:
    ks[VK_SHIFT] = 0x80;
    ks[VK_CONTROL] = 0x80;
    break;
  case 4:
    ks[VK_MENU] = 0x80;
    break;
  case 5:
    // AltGr-style Ctrl+Alt
    ks[VK_CONTROL] = 0x80;
    ks[VK_MENU] = 0x80;
    break;
  default:
    break;
  }
}

/**
 * @brief Builds BMP char maps from ToUnicodeEx for each VK x modifier slice; see charmap.h.
 */
void Switchy_BuildCharMaps(HKL h1, HKL h2)
{
  g_h1 = h1;
  g_h2 = h2;
  ClearMaps();

  if (!h1 || !h2 || h1 == h2)
    return;

  BYTE keyState[256];
  WCHAR buf1[8];
  WCHAR buf2[8];
  UINT wFlags = TO_UNICODE_NO_KEYBOARD_STATE_MUTATION;

  for (UINT vk = 1; vk < 256; vk++)
  {
    UINT scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, h1);
    if (scan == 0)
      scan = MapVirtualKeyExW(vk, MAPVK_VK_TO_VSC, h2);
    if (scan == 0)
      continue;

    for (int si = 0; si < 6; si++)
    {
      ApplyStateIndex(keyState, si);

      int n1 = ToUnicodeEx(vk, scan, keyState, buf1, 8, wFlags, h1);
      int n2 = ToUnicodeEx(vk, scan, keyState, buf2, 8, wFlags, h2);

      // Negative n: dead-key state. n > 1: surrogate/combining; skip for simple BMP map.
      if (n1 < 0 || n2 < 0)
        continue;
      if (n1 == 1 && n2 == 1 && buf1[0] != 0 && buf2[0] != 0)
      {
        WCHAR a = buf1[0];
        WCHAR b = buf2[0];
        if (a != b)
        {
          g_map_l1_to_l2[a] = b;
          g_map_l2_to_l1[b] = a;
        }
      }
    }
  }

  // Remove reverse-map entries for code points that appear in both maps.
  // A character "native" to L1 (has a forward L1->L2 entry) should pass through
  // unchanged when encountered in L2->L1 conversion rather than being remapped to
  // a different L1 key.  This prevents ASCII punctuation shared by both layouts
  // (e.g. '.' and ',' which sit on different physical keys in EN vs RU) from
  // being corrupted when converting mixed-layout text.
  for (int c = 1; c < 65536; c++)
  {
    if (g_map_l1_to_l2[c] != 0 && g_map_l2_to_l1[c] != 0)
      g_map_l2_to_l1[c] = 0;
  }
}

/**
 * @copydoc Switchy_ConvertString
 */
void Switchy_ConvertString(const WCHAR *in, size_t inLen, WCHAR *out, size_t outMax, HKL from, HKL to)
{
  if (!out || outMax == 0)
    return;
  if (inLen + 1 > outMax)
    inLen = outMax - 1;

  const WCHAR *map_fwd = NULL;
  if (from == g_h1 && to == g_h2)
    map_fwd = g_map_l1_to_l2;
  else if (from == g_h2 && to == g_h1)
    map_fwd = g_map_l2_to_l1;
  else
  {
    // No built map for this pair: identity (caller should align layouts with LoadSettings).
    for (size_t i = 0; i < inLen; i++)
      out[i] = in[i];
    out[inLen] = 0;
    return;
  }

  for (size_t i = 0; i < inLen; i++)
  {
    WCHAR c = in[i];
    // 0 in map = unknown: pass through (U+0000 is not stored in normal text).
    if (map_fwd[c] != 0)
      out[i] = map_fwd[c];
    else
      out[i] = c;
  }
  out[inLen] = 0;
}
