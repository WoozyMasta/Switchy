/**
 * @file charmap.h
 * @brief Character mapping between two keyboard layouts for Switchy.
 */

#pragma once
#include <Windows.h>

/**
 * @brief Fills internal maps from two HKLs using ToUnicodeEx / MapVirtualKeyEx
 *        scans (BMP). No-op if NULL or equal.
 * @param h1 First layout.
 * @param h2 Second layout.
 */
void Switchy_BuildCharMaps(HKL h1, HKL h2);

/**
 * @brief Converts text as if re-typed on @p to with the
 *        same physical keys as under @p from.
 * @param in Input string.
 * @param inLen Length of @p in in WCHARs (excluding null).
 * @param out Buffer with room for at least @p inLen + 1 WCHARs.
 * @param outMax Capacity of @p out in WCHARs.
 * @param from Source layout
 *        (must match a handle passed to Switchy_BuildCharMaps).
 * @param to Target layout.
 * @note Unmapped code points pass through. Unknown layout pair: identity copy.
 */
void Switchy_ConvertString(const WCHAR *in, size_t inLen, WCHAR *out,
                           size_t outMax, HKL from, HKL to);
