#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>

/**
   @brief Global configuration and state variables.
*/
HKL hLayout1 = NULL; ///< Handle for the first target layout (e.g., US).
HKL hLayout2 = NULL; ///< Handle for the second target layout (e.g., RU).
UINT hotkeyVkCode = VK_CAPITAL; ///< The key code used to switch layouts (Default: CapsLock).
char iniPath[MAX_PATH]; ///< Path to the configuration file.

HHOOK hHook; ///< Handle to the low-level keyboard hook.
BOOL enabled = TRUE; ///< Master switch for the application.

/* State flags to handle key combinations (Shift+, Alt+) logic */
BOOL appSwitchRequired = FALSE; ///< Flag to toggle app enable/disable state (Alt + Hotkey).
BOOL hotkeyOriginalActionRequired = FALSE;///< Flag indicating if the original key function should be triggered (e.g., toggle CapsLock light).
BOOL hotkeyProcessed = FALSE; ///< Flag indicating the Hotkey is currently held down.
BOOL shiftProcessed = FALSE; ///< Flag indicating Shift is currently held down.

/* Debug logging macro */
#if _DEBUG
  #define LOG(...) printf(__VA_ARGS__)
#else
  #define LOG(...)
#endif

/**
   @brief Displays a critical error message box.
   @param message The error message string.
*/
void ShowError(LPCSTR message)
{
  MessageBox(NULL, message, "Switchy Error", MB_OK | MB_ICONERROR);
}

/**
   @brief Loads configuration from switchy.ini located near the executable.

   Reads target layout IDs and the custom hotkey code.
   If layouts are not defined in ini, tries to detect system layouts.
*/
void LoadSettings()
{
  char exePath[MAX_PATH];
  if (GetModuleFileName(NULL, exePath, MAX_PATH) == 0)
  {
    ShowError("Failed to get executable path.");
    return;
  }

  // Construct path to ini file
  char * lastSlash = strrchr(exePath, '\\');
  if (lastSlash) *lastSlash = '\0';
  snprintf(iniPath, MAX_PATH, "%s\\switchy.ini", exePath);

  // Get system layouts
  HKL * sysLayouts = NULL;
  UINT sysCount = GetKeyboardLayoutList(0, NULL);
  if (sysCount > 0)
  {
    sysLayouts = (HKL *)malloc(sysCount * sizeof(HKL));
    if (sysLayouts) GetKeyboardLayoutList(sysCount, sysLayouts);
  }

  // Get INI layouts
  char layoutStr1[KL_NAMELENGTH] = { 0 };
  char layoutStr2[KL_NAMELENGTH] = { 0 };

  // Read Layouts (Default: empty string to trigger auto-detection)
  GetPrivateProfileString("Settings", "Layout1", "", layoutStr1, KL_NAMELENGTH, iniPath);
  GetPrivateProfileString("Settings", "Layout2", "", layoutStr2, KL_NAMELENGTH, iniPath);

  // Read Custom Hotkey (Default: 20 -> VK_CAPITAL)
  hotkeyVkCode = GetPrivateProfileInt("Settings", "SwitchKey", VK_CAPITAL, iniPath);

  // Layout 1
  hLayout1 = NULL;
  if (strlen(layoutStr1) > 0) {
      hLayout1 = LoadKeyboardLayout(layoutStr1, KLF_NOTELLSHELL);
  }
  if (!hLayout1 && sysLayouts && sysCount > 0) {
      hLayout1 = sysLayouts[0];
      LOG("Layout1 auto-detected: %p\n", hLayout1);
  }

  // Layout 2
  hLayout2 = NULL;
  if (strlen(layoutStr2) > 0) {
      hLayout2 = LoadKeyboardLayout(layoutStr2, KLF_NOTELLSHELL);
  }
  if (!hLayout2 && sysLayouts && sysCount > 0) {
      hLayout2 = (sysCount > 1) ? sysLayouts[1] : sysLayouts[0];
      LOG("Layout2 auto-detected: %p\n", hLayout2);
  }

  if (sysLayouts) free(sysLayouts);

  if (!hLayout1 || !hLayout2)
    ShowError("Could not determine layouts. Check switchy.ini or system settings.");

  LOG("Config Loaded: L1=%p, L2=%p, Hotkey=%d\n", hLayout1, hLayout2, hotkeyVkCode);
}

/**
   @brief Switches the input language of the foreground window.

   Determines the current layout of the active window's thread and swaps
   it between Layout1 and Layout2. If a different layout is active,
   forces Layout1.
*/
void SwitchToSpecificLayout()
{
  if (!hLayout1 || !hLayout2) return;

  HWND hwnd = GetForegroundWindow();
  if (!hwnd) return;

  DWORD threadId = GetWindowThreadProcessId(hwnd, NULL);
  HKL currentLayout = GetKeyboardLayout(threadId);

  // Compare low-word (Language ID) to ignore layout variants (e.g., US-International)
  WORD curLangId = LOWORD(currentLayout);
  WORD l1LangId = LOWORD(hLayout1);

  HKL targetLayout = (curLangId == l1LangId) ? hLayout2 : hLayout1;

  // Request the window to change its input language
  PostMessage(hwnd, WM_INPUTLANGCHANGEREQUEST, 0, (LPARAM)targetLayout);
}

/**
   @brief Simulates a physical key press.
   @param keyCode The virtual key code to simulate.
*/
void PressKey(WORD keyCode)
{
  INPUT input = { 0 };
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = keyCode;
  SendInput(1, &input, sizeof(INPUT));
}

/**
   @brief Simulates a physical key release.
   @param keyCode The virtual key code to simulate.
*/
void ReleaseKey(WORD keyCode)
{
  INPUT input = { 0 };
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = keyCode;
  input.ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(1, &input, sizeof(INPUT));
}

/**
   @brief Simulates the original function of the hotkey.

   Used when the user presses Shift + Hotkey to trigger the original behavior
   (e.g., actually toggling Caps Lock state).
*/
void SimulateOriginalKeyPress()
{
  PressKey((WORD)hotkeyVkCode);
  ReleaseKey((WORD)hotkeyVkCode);
  LOG("Original key function simulated (Code: %d)\n", hotkeyVkCode);
}

/**
   @brief Low-level keyboard hook procedure.

   intercepts keyboard events to handle layout switching and hotkey logic.
*/
LRESULT CALLBACK HandleKeyboardEvent(int nCode, WPARAM wParam, LPARAM lParam)
{
  KBDLLHOOKSTRUCT * key = (KBDLLHOOKSTRUCT *)lParam;

  if (nCode == HC_ACTION && !(key->flags & LLKHF_INJECTED))
  {

    // --- Handle the configured Hotkey ---
    if (key->vkCode == hotkeyVkCode)
    {

      // Alt + Hotkey -> Toggle App Enable/Disable
      if (wParam == WM_SYSKEYDOWN)
      {
        appSwitchRequired = TRUE;
        hotkeyProcessed = TRUE;
        return 1;
      }

      // Release Alt + Hotkey
      if (wParam == WM_SYSKEYUP || (wParam == WM_KEYUP && appSwitchRequired))
      {
        enabled = !enabled;
        appSwitchRequired = FALSE;
        hotkeyProcessed = FALSE;
        LOG("Switchy has been %s\n", enabled ? "enabled" : "disabled");
        return 1;
      }

      // Hotkey Pressed
      if (wParam == WM_KEYDOWN)
      {
        hotkeyProcessed = TRUE;
        if (enabled)
        {
          // If Shift is held, we prepare to trigger the original key function later
          if (shiftProcessed)
            hotkeyOriginalActionRequired = TRUE;
          return 1; // Block default processing
        }
      }
      // Hotkey Released
      else if (wParam == WM_KEYUP)
      {
        hotkeyProcessed = FALSE;
        if (enabled)
        {
          if (shiftProcessed || hotkeyOriginalActionRequired)
          {
            // Shift was involved: Perform original action (e.g. toggle CapsLock)
            SimulateOriginalKeyPress();
            hotkeyOriginalActionRequired = FALSE;
          }
          else
          {
            // Clean press: Switch Layout
            SwitchToSpecificLayout();
          }
          return 1;
        }
      }
    }

    // --- Handle Alt (Context for app toggling) ---
    else if (key->vkCode == VK_LMENU || key->vkCode == VK_RMENU)
    {
      if (wParam == WM_SYSKEYDOWN && hotkeyProcessed)
        appSwitchRequired = TRUE;
    }

    // --- Handle Shift (Context for original key action) ---
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

  return CallNextHookEx(NULL, nCode, wParam, lParam);
}

/**
   @brief Entry point.
*/
int main()
{
  // Prevent multiple instances
  HANDLE hMutex = CreateMutex(0, 0, "Switchy_CustomLayouts");
  if (GetLastError() == ERROR_ALREADY_EXISTS)
  {
    ShowError("Another instance of Switchy is already running!");
    return 1;
  }

  LoadSettings();

  hHook = SetWindowsHookEx(WH_KEYBOARD_LL, HandleKeyboardEvent, 0, 0);
  if (hHook == NULL)
  {
    ShowError("Error calling 'SetWindowsHookEx'");
    return 1;
  }

  MSG messages;
  while (GetMessage(&messages, NULL, 0, 0))
  {
    TranslateMessage(&messages);
    DispatchMessage(&messages);
  }

  UnhookWindowsHookEx(hHook);
  if (hMutex) ReleaseMutex(hMutex);

  return 0;
}
