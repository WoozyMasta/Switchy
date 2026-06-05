/**
 * @file switchy.c
 * @brief Keyboard hook, layout switching,
 *        and optional clipboard-based layout conversion.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

#include "charmap.h"

#ifndef WM_INPUTLANGCHANGEREQUEST
#define WM_INPUTLANGCHANGEREQUEST                                              \
  0x0050 ///< WinUser: request input language change for a thread.
#endif

#ifndef TO_UNICODE_NO_KEYBOARD_STATE_MUTATION
#define TO_UNICODE_NO_KEYBOARD_STATE_MUTATION                                  \
  0x4 ///< ToUnicodeEx: do not mutate global keyboard state (Win10 1607+).
#endif

#ifndef WM_SWITCHY_DEFER
#define WM_SWITCHY_DEFER                                                       \
  (WM_APP +                                                                    \
   0x100) ///< Thread message: run TryConvertSelection outside WH_KEYBOARD_LL.
#endif

#define MAX_EXCLUDE                                                            \
  128 ///< Max executable basenames per ExcludeSwitch / ExcludeConvert.
#define SENDMSG_TIMEOUT_MS_DEFAULT                                             \
  80 ///< Default timeout for SendMessageTimeout layout requests (ms).
#define CLIPBOARD_MAX_WCHARS                                                   \
  (1024 * 1024) ///< Max WCHAR count for clipboard backup/convert (~2 MiB).
#define CLIPBOARD_POST_COPY_DELAY_MS                                           \
  40 ///< After synthetic Ctrl+C, wait for CF_UNICODETEXT.
#define CLIPBOARD_POST_PASTE_DELAY_MS                                          \
  30 ///< After paste, before restoring prior clipboard.
#define SYS_LAYOUTS_STACK_MAX                                                  \
  64 ///< Stack buffer for GetKeyboardLayoutList (avoids malloc when enough).

/**
 * @name Global configuration (set by LoadSettings)
 * @{
 */

HKL hLayout1 = NULL; ///< First layout handle from INI or auto-detected.
HKL hLayout2 = NULL; ///< Second layout handle from INI or auto-detected.
UINT hotkeyVkCode = VK_CAPITAL; ///< Virtual key for layout switch.
WCHAR iniPath[MAX_PATH]; ///< Full path to switchy.ini beside the executable.

HHOOK hHook; ///< Low-level keyboard hook instance.
BOOL enabled = TRUE; ///< Master enable; Alt+hotkey toggles enabled state.

BOOL appSwitchRequired = FALSE; ///< Alt went down while hotkey active:
                                ///< next Alt keyup toggles enabled.
BOOL hotkeyOriginalActionRequired =
    FALSE; ///< Shift+hotkey: later inject real key press (e.g. Caps LED).
BOOL hotkeyProcessed = FALSE; ///< Hotkey currently held (Alt/Shift context).
BOOL shiftProcessed = FALSE; ///< Shift held (passthrough Caps).

BOOL convertWithCtrl = TRUE; ///< INI: Ctrl+switch runs conversion when TRUE.
BOOL smartCaps =
    FALSE; ///< INI: plain switch may run synthetic Ctrl+C conversion.
int fallbackCycleHotkey =
    0; ///< 0=none, 1=Alt+Shift, 2=Ctrl+Shift, 3=Win+Space.
int sendMsgTimeoutMs =
    SENDMSG_TIMEOUT_MS_DEFAULT; ///< INI: SwitchTimeoutMs -
                                ///< SendMessageTimeout for layout requests.

WCHAR *excludeSwitch[MAX_EXCLUDE]; ///< Lowercase exe basenames; no layout
                                   ///< switch in these apps.
WCHAR *excludeConvert[MAX_EXCLUDE]; ///< Lowercase exe basenames; no Ctrl
                                    ///< conversion in these apps.
int excludeSwitchCount = 0; ///< Number of entries in excludeSwitch.
int excludeConvertCount = 0; ///< Number of entries in excludeConvert.

static WCHAR *clipboardBackup = NULL; ///< Pre-copy CF_UNICODETEXT snapshot;
                                      ///< EmptyClipboard drops other formats.

/** @} */

#if _DEBUG
#define LOG(...)                                                               \
  wprintf(__VA_ARGS__) ///< Debug: print wide messages to the console.
#else
#define LOG(...)
#endif

/**
 * @brief Shows a modal error dialog.
 * @param message UTF-16 message text.
 */
static void ShowErrorW(const WCHAR *message)
{
  MessageBoxW(NULL, message, L"Switchy Error", MB_OK | MB_ICONERROR);
}

/**
 * @brief Duplicates a wide string with malloc.
 * @param s Source string.
 * @return New buffer or NULL on failure.
 */
static WCHAR *DupW(const WCHAR *s)
{
  size_t n = (wcslen(s) + 1) * sizeof(WCHAR);
  WCHAR *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}

/**
 * @brief Frees all entries in an exclude list and resets the count.
 * @param list List of strings.
 * @param count Number of entries in the list.
 */
static void FreeExcludeList(WCHAR **list, int *count)
{
  for (int i = 0; i < *count; i++)
  {
    free(list[i]);
    list[i] = NULL;
  }
  *count = 0;
}

/**
 * @brief Lowercases a wide string in place (towlower per character).
 * @param s String to lowercase.
 */
static void LowerW(WCHAR *s)
{
  for (; *s; s++)
    *s = (WCHAR)towlower((wint_t)*s);
}

/**
 * @brief Loads INI section keys (before '=') as lowercase basenames into out.
 * @param section Section name.
 * @param out List of strings.
 * @param outCount Number of entries in the list.
 */
static void LoadExcludeSection(const WCHAR *section, WCHAR **out, int *outCount)
{
  // GetPrivateProfileSectionW returns "key=value\0key2=value2\0\0". Values are
  // ignored.
  WCHAR buf[32768];
  DWORD n = GetPrivateProfileSectionW(section, buf, 32768, iniPath);
  if (n == 0)
    return;

  WCHAR *p = buf;
  while (*p && *outCount < MAX_EXCLUDE)
  {
    WCHAR key[260];
    WCHAR *eq = wcschr(p, L'=');
    if (eq)
    {
      size_t len = (size_t)(eq - p);
      if (len >= 260)
        len = 259;
      wmemcpy(key, p, len);
      key[len] = 0;
    }
    else
    {
      wcsncpy(key, p, 259);
      key[259] = 0;
    }
    LowerW(key);
    WCHAR *copy = DupW(key);
    if (copy)
      out[(*outCount)++] = copy;
    p += wcslen(p) + 1;
  }
}

/**
 * @brief Returns whether the foreground process basename
 *        is in an excludetable.
 * @param forSwitch TRUE = ExcludeSwitch, FALSE = ExcludeConvert.
 * @return TRUE if the foreground process basename is in an exclude table.
 */
static BOOL IsForegroundExcluded(BOOL forSwitch)
{
  int n = forSwitch ? excludeSwitchCount : excludeConvertCount;
  WCHAR **list = forSwitch ? excludeSwitch : excludeConvert;
  if (n == 0)
    return FALSE;

  HWND w = GetForegroundWindow();
  if (!w)
    return FALSE;

  // Resolve full image path and compare basename only (lowercase),
  // per INI convention
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!h)
    return FALSE;

  WCHAR path[MAX_PATH * 2];
  DWORD size = (DWORD)(sizeof(path) / sizeof(path[0]));
  BOOL ok = QueryFullProcessImageNameW(h, 0, path, &size);
  CloseHandle(h);
  if (!ok)
    return FALSE;

  WCHAR *base = wcsrchr(path, L'\\');
  base = base ? base + 1 : path;
  WCHAR lower[260];
  wcsncpy(lower, base, 259);
  lower[259] = 0;
  LowerW(lower);

  for (int i = 0; i < n; i++)
  {
    if (!wcscmp(lower, list[i]))
      return TRUE;
  }
  return FALSE;
}

/**
 * @brief Reads switchy.ini next to the executable
 *        and fills globals and char maps.
 * @return FALSE if both layouts could not be resolved
 *         (caller must not installthe hook).
 */
BOOL LoadSettings(void)
{
  WCHAR exePath[MAX_PATH];
  if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0)
  {
    ShowErrorW(L"Failed to get executable path.");
    return FALSE;
  }

  // Strip exe name so iniPath is the directory
  // (shortcuts still resolve to real exe dir)
  WCHAR *lastSlash = wcsrchr(exePath, L'\\');
  if (lastSlash)
    *lastSlash = 0;
  if (wcslen(exePath) + 14 >= MAX_PATH)
  {
    ShowErrorW(L"Executable path too long.");
    return FALSE;
  }
  wcscpy(iniPath, exePath);
  wcscat(iniPath, L"\\switchy.ini");

  FreeExcludeList(excludeSwitch, &excludeSwitchCount);
  FreeExcludeList(excludeConvert, &excludeConvertCount);
  LoadExcludeSection(L"ExcludeSwitch", excludeSwitch, &excludeSwitchCount);
  LoadExcludeSection(L"ExcludeConvert", excludeConvert, &excludeConvertCount);

  // Snapshot system layouts once for auto-fill when Layout1/2 are empty in INI
  HKL stackLayouts[SYS_LAYOUTS_STACK_MAX];
  HKL *sysLayouts = NULL;
  UINT sysCount = 0;
  BOOL sysLayoutsHeap = FALSE;

  UINT needLayouts = GetKeyboardLayoutList(0, NULL);
  if (needLayouts > 0)
  {
    if (needLayouts <= SYS_LAYOUTS_STACK_MAX)
    {
      sysCount = GetKeyboardLayoutList(needLayouts, stackLayouts);
      sysLayouts = stackLayouts;
    }
    else
    {
      sysLayouts = (HKL *)malloc(needLayouts * sizeof(HKL));
      if (sysLayouts)
      {
        sysCount = GetKeyboardLayoutList(needLayouts, sysLayouts);
        sysLayoutsHeap = TRUE;
      }
      else
      {
        sysCount = GetKeyboardLayoutList(SYS_LAYOUTS_STACK_MAX, stackLayouts);
        sysLayouts = stackLayouts;
      }
    }
  }

  WCHAR layoutStr1[KL_NAMELENGTH] = {0};
  WCHAR layoutStr2[KL_NAMELENGTH] = {0};

  GetPrivateProfileStringW(L"Settings", L"Layout1", L"", layoutStr1,
                           KL_NAMELENGTH, iniPath);
  GetPrivateProfileStringW(L"Settings", L"Layout2", L"", layoutStr2,
                           KL_NAMELENGTH, iniPath);

  hotkeyVkCode = (UINT)GetPrivateProfileIntW(L"Settings", L"SwitchKey",
                                             VK_CAPITAL, iniPath);
  convertWithCtrl =
      GetPrivateProfileIntW(L"Settings", L"ConvertWithCtrl", 1, iniPath) != 0;
  smartCaps = GetPrivateProfileIntW(L"Settings", L"SmartCaps", 0, iniPath) != 0;
  fallbackCycleHotkey =
      GetPrivateProfileIntW(L"Settings", L"FallbackCycleHotkey", 0, iniPath);
  sendMsgTimeoutMs = GetPrivateProfileIntW(L"Settings", L"SwitchTimeoutMs",
                                           SENDMSG_TIMEOUT_MS_DEFAULT, iniPath);
  if (sendMsgTimeoutMs < 10)
    sendMsgTimeoutMs = 10;
  if (sendMsgTimeoutMs > 2000)
    sendMsgTimeoutMs = 2000;

  hLayout1 = NULL;
  if (layoutStr1[0])
    hLayout1 = LoadKeyboardLayoutW(layoutStr1, KLF_NOTELLSHELL);
  if (!hLayout1 && sysLayouts && sysCount > 0)
  {
    hLayout1 = sysLayouts[0];
    LOG(L"Layout1 auto-detected: %p\n", hLayout1);
  }

  hLayout2 = NULL;
  if (layoutStr2[0])
    hLayout2 = LoadKeyboardLayoutW(layoutStr2, KLF_NOTELLSHELL);
  if (!hLayout2 && sysLayouts && sysCount > 0)
  {
    // If only one system layout exists,
    // both Switchy slots may point to it until user adds another
    hLayout2 = (sysCount > 1) ? sysLayouts[1] : sysLayouts[0];
    LOG(L"Layout2 auto-detected: %p\n", hLayout2);
  }

  if (sysLayoutsHeap)
    free(sysLayouts);

  if (!hLayout1 || !hLayout2)
  {
    ShowErrorW(
        L"Could not determine layouts. Check switchy.ini or system settings.");
    return FALSE;
  }

  Switchy_BuildCharMaps(hLayout1, hLayout2);
  LOG(L"Config: L1=%p L2=%p Hotkey=%u SmartCaps=%d\n", hLayout1, hLayout2,
      hotkeyVkCode, smartCaps);
  return TRUE;
}

/**
 * @brief Frees the in-memory clipboard backup buffer.
 */
static void FreeClipboardBackup(void)
{
  free(clipboardBackup);
  clipboardBackup = NULL;
}

/**
 * @brief Result of snapshotting CF_UNICODETEXT before a synthetic copy.
 */
typedef enum
{
  BackupCb_OK, ///< Unicode text captured (may be empty string).
  BackupCb_NoText, ///< No CF_UNICODETEXT or empty handle.
  BackupCb_TooLarge, ///< Exceeds CLIPBOARD_MAX_WCHARS.
  BackupCb_OpenFailed ///< OpenClipboard failed.
} BackupCbResult;

typedef enum
{
  ClipCopy_OK,
  ClipCopy_TooLarge,
  ClipCopy_AllocFail
} ClipCopyResult;

/**
 * @brief Copies bounded UTF-16 from a CF_UNICODETEXT global
 *        (caller holds clipboard open).
 */
static ClipCopyResult CopyBoundedUnicodeFromHGlobal(HANDLE hMem, WCHAR **out,
                                                    size_t *outLen)
{
  *out = NULL;
  *outLen = 0;
  SIZE_T gsz = GlobalSize(hMem);
  if (gsz == 0 || gsz > (CLIPBOARD_MAX_WCHARS + 1) * sizeof(WCHAR))
    return ClipCopy_TooLarge;

  WCHAR *p = GlobalLock(hMem);
  if (!p)
    return ClipCopy_AllocFail;

  size_t maxWchars = gsz / sizeof(WCHAR);
  size_t n = 0;
  while (n < maxWchars && n < CLIPBOARD_MAX_WCHARS && p[n])
    n++;
  if (n == CLIPBOARD_MAX_WCHARS && p[n] != 0)
  {
    GlobalUnlock(hMem);
    return ClipCopy_TooLarge;
  }

  WCHAR *buf = malloc((n + 1) * sizeof(WCHAR));
  if (!buf)
  {
    GlobalUnlock(hMem);
    return ClipCopy_AllocFail;
  }
  memcpy(buf, p, (n + 1) * sizeof(WCHAR));
  GlobalUnlock(hMem);
  *out = buf;
  *outLen = n;
  return ClipCopy_OK;
}

/**
 * @brief Copies current CF_UNICODETEXT into @c clipboardBackup (bounded).
 */
static BackupCbResult BackupUnicodeClipboard(void)
{
  FreeClipboardBackup();
  if (!OpenClipboard(NULL))
    return BackupCb_OpenFailed;

  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (!h)
  {
    CloseClipboard();
    return BackupCb_NoText;
  }

  size_t n;
  ClipCopyResult cr = CopyBoundedUnicodeFromHGlobal(h, &clipboardBackup, &n);
  CloseClipboard();

  if (cr == ClipCopy_TooLarge)
    return BackupCb_TooLarge;
  if (cr == ClipCopy_AllocFail)
    return BackupCb_OpenFailed;
  return BackupCb_OK;
}

/**
 * @brief Replaces clipboard with a single CF_UNICODETEXT block.
 * @param s Null-terminated Unicode string
 *        (owns nothing; copied into global alloc).
 */
static BOOL SetClipboardUnicodeText(const WCHAR *s)
{
  size_t n = (wcslen(s) + 1) * sizeof(WCHAR);
  HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
  if (!h)
    return FALSE;

  void *p = GlobalLock(h);
  if (!p)
  {
    GlobalFree(h);
    return FALSE;
  }

  memcpy(p, s, n);
  GlobalUnlock(h);
  if (!OpenClipboard(NULL))
  {
    GlobalFree(h);
    return FALSE;
  }

  EmptyClipboard();
  if (!SetClipboardData(CF_UNICODETEXT, h))
  {
    GlobalFree(h);
    CloseClipboard();
    return FALSE;
  }
  CloseClipboard();
  return TRUE;
}

/**
 * @brief Restores clipboard from clipboardBackup and clears the backup pointer.
 * @warning EmptyClipboard clears all formats;
 *          only CF_UNICODETEXT is restored(see README).
 */
static void RestoreUnicodeClipboard(void)
{
  if (!OpenClipboard(NULL))
  {
    FreeClipboardBackup();
    return;
  }
  EmptyClipboard();
  if (clipboardBackup)
  {
    size_t n = (wcslen(clipboardBackup) + 1) * sizeof(WCHAR);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
    if (h)
    {
      void *p = GlobalLock(h);
      if (p)
      {
        memcpy(p, clipboardBackup, n);
        GlobalUnlock(h);
        if (!SetClipboardData(CF_UNICODETEXT, h))
          GlobalFree(h);
      }
      else
        GlobalFree(h);
    }
  }
  CloseClipboard();
  FreeClipboardBackup();
}

/**
 * @brief Reads CF_UNICODETEXT with length and allocation bounded
 *        by CLIPBOARD_MAX_WCHARS.
 * @param[out] out Allocated string (caller frees); set NULL on failure.
 * @param[out] outLen Character count excluding null.
 * @return FALSE if missing, too large, or allocation failed.
 */
static BOOL ReadClipboardUnicodeLimited(WCHAR **out, size_t *outLen)
{
  *out = NULL;
  *outLen = 0;
  if (!OpenClipboard(NULL))
    return FALSE;

  HANDLE h = GetClipboardData(CF_UNICODETEXT);
  if (!h)
  {
    CloseClipboard();
    return FALSE;
  }

  ClipCopyResult cr = CopyBoundedUnicodeFromHGlobal(h, out, outLen);
  CloseClipboard();
  if (cr != ClipCopy_OK)
  {
    free(*out);
    *out = NULL;
    *outLen = 0;
    return FALSE;
  }
  return TRUE;
}

/**
 * @brief Sends one key-down via SendInput.
 * @param keyCode Virtual key code.
 */
static void PressKey(WORD keyCode)
{
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = keyCode;
  SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Sends one key-up via SendInput.
 * @param keyCode Virtual key code.
 */
static void ReleaseKey(WORD keyCode)
{
  INPUT input = {0};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = keyCode;
  input.ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &input, sizeof(INPUT));
}

/**
 * @brief Injects a short press of the configured switch key
 *        (e.g. Caps Lock toggle).
 */
static void SimulateOriginalKeyPress(void)
{
  PressKey((WORD)hotkeyVkCode);
  ReleaseKey((WORD)hotkeyVkCode);
  LOG(L"Original key simulated vk=%u\n", hotkeyVkCode);
}

/**
 * @brief Sends Alt+Shift, Ctrl+Shift,
 *        or Win+Space per fallbackCycleHotkey if layout stuck.
 */
static void SendSyntheticCycleHotkey(void)
{
  if (fallbackCycleHotkey == 1)
  {
    PressKey(VK_MENU);
    PressKey(VK_LSHIFT);
    ReleaseKey(VK_MENU);
    ReleaseKey(VK_LSHIFT);
  }
  else if (fallbackCycleHotkey == 2)
  {
    PressKey(VK_CONTROL);
    PressKey(VK_LSHIFT);
    ReleaseKey(VK_CONTROL);
    ReleaseKey(VK_LSHIFT);
  }
  else if (fallbackCycleHotkey == 3)
  {
    PressKey(VK_LWIN);
    PressKey(VK_SPACE);
    ReleaseKey(VK_SPACE);
    ReleaseKey(VK_LWIN);
  }
}

/**
 * @brief Activates target HKL for the foreground thread: focus window,
 *        then root, AttachThreadInput, then cycle hotkey.
 * @param target Layout handle to apply.
 */
static void SwitchToLayoutHKL(HKL target)
{
  if (!hLayout1 || !hLayout2 || !target)
    return;
  if (IsForegroundExcluded(TRUE))
    return;

  HWND hwnd = GetForegroundWindow();
  if (!hwnd)
    return;

  DWORD tid = GetWindowThreadProcessId(hwnd, NULL);

  GUITHREADINFO gti;
  memset(&gti, 0, sizeof(gti));
  gti.cbSize = sizeof(GUITHREADINFO);
  HWND hwndTarget = hwnd;
  if (GetGUIThreadInfo(tid, &gti) && gti.hwndFocus && IsWindow(gti.hwndFocus))
    hwndTarget = gti.hwndFocus;

  DWORD_PTR smres = 0;
  SendMessageTimeoutW(hwndTarget, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target,
                      SMTO_ABORTIFHUNG, (UINT)sendMsgTimeoutMs, &smres);

  // Some hosts only route layout changes through the root window
  if (GetKeyboardLayout(tid) != target && hwndTarget != hwnd)
  {
    SendMessageTimeoutW(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)target,
                        SMTO_ABORTIFHUNG, (UINT)sendMsgTimeoutMs, &smres);
  }

  // Last API attempt: same-thread input attach lets ActivateKeyboardLayout hit
  // the foreign queue
  if (GetKeyboardLayout(tid) != target)
  {
    DWORD curTid = GetCurrentThreadId();
    if (AttachThreadInput(curTid, tid, TRUE))
    {
      ActivateKeyboardLayout(target, 0);
      AttachThreadInput(curTid, tid, FALSE);
    }
  }

  if (GetKeyboardLayout(tid) != target && fallbackCycleHotkey != 0)
    SendSyntheticCycleHotkey();
}

/**
 * @brief Toggles between hLayout1 and hLayout2 for the foreground thread.
 */
void SwitchToSpecificLayout(void)
{
  if (!hLayout1 || !hLayout2)
    return;

  HWND hwnd = GetForegroundWindow();
  if (!hwnd)
    return;

  DWORD tid = GetWindowThreadProcessId(hwnd, NULL);
  HKL cur = GetKeyboardLayout(tid);
  HKL target;

  // Unknown third layout: always jump toward hLayout1
  // then user toggles between the pair
  if (cur == hLayout1)
    target = hLayout2;
  else if (cur == hLayout2)
    target = hLayout1;
  else
    target = hLayout1;

  SwitchToLayoutHKL(target);
}

/**
 * @brief How copy/paste keys are injected for conversion.
 */
typedef enum
{
  ConvertInput_CtrlHeld, ///< User holds Ctrl; only C/V keys are sent.
  ConvertInput_SyntheticCtrl ///< Full left-Ctrl chord (plain SmartCaps path).
} ConvertInputKind;

/**
 * @brief Sends left Ctrl down, vk down/up, left Ctrl up
 *        (VK_LCONTROL avoids ambiguous generic Ctrl).
 * @param vk Virtual key code.
 */
static void SendSyntheticCtrlChord(WORD vk)
{
  INPUT in[4];
  memset(in, 0, sizeof(in));
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = VK_LCONTROL;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wVk = vk;
  in[2].type = INPUT_KEYBOARD;
  in[2].ki.wVk = vk;
  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  in[3].type = INPUT_KEYBOARD;
  in[3].ki.wVk = VK_LCONTROL;
  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, in, sizeof(INPUT));
}

/**
 * @brief Whether the clipboard after copy matches the pre-copy backup
 *        (no selection often leaves it unchanged).
 * @param after Text after copy.
 * @param afterLen Length in WCHARs excluding null.
 * @return Nonzero if @a after equals the backed-up snapshot.
 */
static BOOL ClipboardUnchangedAfterCopy(const WCHAR *after, size_t afterLen)
{
  if (!clipboardBackup)
    return afterLen == 0;
  if (afterLen == 0)
    return clipboardBackup[0] == 0;
  return wcscmp(clipboardBackup, after) == 0;
}

/**
 * @brief Frees optional text, restores clipboard, optionally switches layout
 *        (aborted conversion paths).
 */
static void AbortTryConvert(WCHAR *optionalText, BOOL switchLayoutOnEmpty)
{
  free(optionalText);
  RestoreUnicodeClipboard();
  if (switchLayoutOnEmpty)
    SwitchToSpecificLayout();
}

/**
 * @brief Queues TryConvertSelection on this thread
 *        (never call SendInput from inside hook).
 * @return TRUE if the message was posted.
 */
static BOOL PostDeferConvert(ConvertInputKind kind, BOOL switchOnEmpty)
{
  if (PostThreadMessageW(GetCurrentThreadId(), WM_SWITCHY_DEFER, (WPARAM)kind,
                         (LPARAM)switchOnEmpty))
    return TRUE;
  LOG(L"PostThreadMessage WM_SWITCHY_DEFER failed\n");
  return FALSE;
}

/**
 * @brief Copy -> convert -> paste pipeline, then switch to the target layout.
 * @param kind ConvertInput_CtrlHeld: user holds Ctrl, only C/V are injected.
 *             ConvertInput_SyntheticCtrl: Ctrl+C / Ctrl+V (plain SmartCaps).
 *
 * @param switchLayoutOnEmpty If TRUE, call SwitchToSpecificLayout when we skip
 *        conversion (empty, unchanged clipboard, backup failure, etc.).
 * @note Runs on the message thread via WM_SWITCHY_DEFER,
 *       not inside WH_KEYBOARD_LL (see hook handler).
 */
static void TryConvertSelection(ConvertInputKind kind, BOOL switchLayoutOnEmpty)
{
  if (!enabled)
    return;
  if (kind == ConvertInput_CtrlHeld && !convertWithCtrl)
    return;
  if (kind == ConvertInput_SyntheticCtrl && !smartCaps)
    return;

  if (IsForegroundExcluded(FALSE))
  {
    if (switchLayoutOnEmpty)
      SwitchToSpecificLayout();
    return;
  }

  HWND hwnd = GetForegroundWindow();
  if (!hwnd)
    return;

  DWORD tid = GetWindowThreadProcessId(hwnd, NULL);
  HKL from = GetKeyboardLayout(tid);
  HKL to;

  // Convert toward the "other" configured layout;
  // unknown current maps to hLayout1 -> hLayout2
  if (from == hLayout1)
    to = hLayout2;
  else if (from == hLayout2)
    to = hLayout1;
  else
    to = hLayout1;

  BackupCbResult br = BackupUnicodeClipboard();
  if (br == BackupCb_TooLarge || br == BackupCb_OpenFailed)
  {
    if (switchLayoutOnEmpty)
      SwitchToSpecificLayout();
    return;
  }

  // Inject copy: CtrlHeld assumes real Ctrl is down;
  // Synthetic sends full Ctrl+C
  if (kind == ConvertInput_CtrlHeld)
  {
    INPUT cin[2] = {0};
    cin[0].type = INPUT_KEYBOARD;
    cin[0].ki.wVk = 'C';
    cin[1].type = INPUT_KEYBOARD;
    cin[1].ki.wVk = 'C';
    cin[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, cin, sizeof(INPUT));
  }
  else
    SendSyntheticCtrlChord('C');

  Sleep(CLIPBOARD_POST_COPY_DELAY_MS);

  WCHAR *text = NULL;
  size_t textLen = 0;
  if (!ReadClipboardUnicodeLimited(&text, &textLen))
  {
    AbortTryConvert(text, switchLayoutOnEmpty);
    return;
  }

  // Empty or identical to pre-copy snapshot ->treat as "no selection";
  // do not paste
  if (textLen == 0 || ClipboardUnchangedAfterCopy(text, textLen))
  {
    AbortTryConvert(text, switchLayoutOnEmpty);
    return;
  }

  WCHAR *out = malloc((textLen + 2) * sizeof(WCHAR));
  if (!out)
  {
    AbortTryConvert(text, switchLayoutOnEmpty);
    return;
  }

  Switchy_ConvertString(text, textLen, out, textLen + 2, from, to);
  free(text);

  if (!SetClipboardUnicodeText(out))
  {
    free(out);
    AbortTryConvert(NULL, switchLayoutOnEmpty);
    return;
  }
  free(out);

  // Paste converted text; again CtrlHeld uses real Ctrl for V
  if (kind == ConvertInput_CtrlHeld)
  {
    INPUT vin[2] = {0};
    vin[0].type = INPUT_KEYBOARD;
    vin[0].ki.wVk = 'V';
    vin[1].type = INPUT_KEYBOARD;
    vin[1].ki.wVk = 'V';
    vin[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, vin, sizeof(INPUT));
  }
  else
    SendSyntheticCtrlChord('V');

  Sleep(CLIPBOARD_POST_PASTE_DELAY_MS);

  RestoreUnicodeClipboard();

  if (!IsForegroundExcluded(TRUE))
    SwitchToLayoutHKL(to);
}

/**
 * @brief WH_KEYBOARD_LL: switch key, Alt toggle, Shift+passthrough.
 *        Skips LLKHF_INJECTED (our SendInput).
 * @param nCode Hook code.
 * @param wParam Window message.
 * @param lParam Keyboard event.
 * @return 1 if the event was handled, 0 if it was not.
 * @note Ctrl is read with GetAsyncKeyState on keyup (not hook counters).
 */
LRESULT CALLBACK HandleKeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam)
{
  KBDLLHOOKSTRUCT *key = (KBDLLHOOKSTRUCT *)lParam;

  if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED))
  {
    if (key->vkCode == hotkeyVkCode)
    {
      // Alt+hotkey uses WM_SYSKEY* so we can tell Alt apart from plain hotkey.
      if (wParam == WM_SYSKEYDOWN)
      {
        appSwitchRequired = TRUE;
        hotkeyProcessed = TRUE;
        return 1;
      }

      /* Alt released: toggle enabled state for the hook. */
      if (wParam == WM_SYSKEYUP || (wParam == WM_KEYUP && appSwitchRequired))
      {
        enabled = !enabled;
        appSwitchRequired = FALSE;
        hotkeyProcessed = FALSE;
        LOG(L"Switchy %s\n", enabled ? L"enabled" : L"disabled");
        return 1;
      }

      if (wParam == WM_KEYDOWN)
      {
        hotkeyProcessed = TRUE;
        if (enabled)
        {
          // Shift+hotkey: later we synthesize a real key toggle (Caps LED etc.)
          if (shiftProcessed)
            hotkeyOriginalActionRequired = TRUE;
          return 1;
        }
      }
      else if (wParam == WM_KEYUP)
      {
        hotkeyProcessed = FALSE;
        if (enabled)
        {
          if (shiftProcessed || hotkeyOriginalActionRequired)
          {
            SimulateOriginalKeyPress();
            hotkeyOriginalActionRequired = FALSE;
          }
          else
          {
            BOOL ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            // Defer TryConvertSelection: SendInput must not run inside this
            // callback or synthetic Ctrl+C/V interleaves with the
            // still-processing hotkey release in some hosts.
            if (ctrlDown && convertWithCtrl)
            {
              if (!PostDeferConvert(ConvertInput_CtrlHeld, smartCaps))
                SwitchToSpecificLayout();
            }
            else if (!ctrlDown && smartCaps)
            {
              if (!PostDeferConvert(ConvertInput_SyntheticCtrl, TRUE))
                SwitchToSpecificLayout();
            }
            else
              SwitchToSpecificLayout();
          }
          return 1;
        }
      }
    }

    else if (key->vkCode == VK_LMENU || key->vkCode == VK_RMENU)
    {
      if (wParam == WM_SYSKEYDOWN && hotkeyProcessed)
        appSwitchRequired = TRUE;
    }

    else if (key->vkCode == VK_LSHIFT || key->vkCode == VK_RSHIFT)
    {
      if (wParam == WM_KEYDOWN)
      {
        shiftProcessed = TRUE;
        if (hotkeyProcessed)
          hotkeyOriginalActionRequired = TRUE;
      }
      else if (wParam == WM_KEYUP)
        shiftProcessed = FALSE;
    }
  }

  return CallNextHookEx(hHook, nCode, wParam, lParam);
}

/**
 * @brief Entry: single instance, load INI, install hook,
 *        message loop (WM_SWITCHY_DEFER before TranslateMessage).
 */
int main(void)
{
  HANDLE hMutex = CreateMutexW(0, 0, L"Switchy_CustomLayouts");
  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    ShowErrorW(L"Another instance of Switchy is already running!");
    return 1;
  }

  if (!LoadSettings())
  {
    CloseHandle(hMutex);
    return 1;
  }

  hHook = SetWindowsHookExW(WH_KEYBOARD_LL, HandleKeyboardEvent, 0, 0);
  if (hHook == NULL)
  {
    ShowErrorW(L"Error calling SetWindowsHookEx");
    CloseHandle(hMutex);
    return 1;
  }

  MSG messages;
  BOOL gm;
  for (;;)
  {
    gm = GetMessageW(&messages, NULL, 0, 0);
    if (gm == 0)
      break; /* WM_QUIT */
    if (gm == (BOOL)-1)
      break; /* documented error return */

    if (messages.message == WM_SWITCHY_DEFER)
    {
      TryConvertSelection((ConvertInputKind)messages.wParam,
                          (BOOL)messages.lParam);
      continue;
    }
    TranslateMessage(&messages);
    DispatchMessageW(&messages);
  }

  UnhookWindowsHookEx(hHook);
  if (hMutex)
    CloseHandle(hMutex);

  FreeExcludeList(excludeSwitch, &excludeSwitchCount);
  FreeExcludeList(excludeConvert, &excludeConvertCount);
  FreeClipboardBackup();
  return gm == (BOOL)-1 ? 1 : 0;
}
