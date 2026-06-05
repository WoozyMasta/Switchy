# Switchy

<!-- markdownlint-disable-next-line MD033 -->
<img src="switchy.png" alt="Switchy" align="right" width="128">

A lightweight utility to toggle between two specific keyboard layouts
using **Caps Lock** (or any other specified key).

Unlike the standard Windows behavior
(which cycles through all installed layouts),
**Switchy** allows you to define exactly **two main layouts**
in a configuration file.  
Pressing the key will instantly toggle between them,
ignoring any other layouts installed on the system.

![switchy]

Layout switching targets the **focused** control when possible
(`GetGUIThreadInfo` + `SendMessageTimeout` with `WM_INPUTLANGCHANGEREQUEST`),
with fallbacks documented in code.
This avoids relying only on `PostMessage` to the top-level window
(which often fails in modal dialogs such as **Save As**).
It still does not show the Windows 10/11 language pop-up by default.

## Usage

### MSI installer (recommended)

1. Download `switchy.msi` from the [Releases] page.
1. Run the installer - Switchy is placed in `%ProgramFiles%\Switchy\`
   and registered for autostart via the current user's Run key.
1. The application starts automatically once the installation is complete.

To configure, edit `switchy.ini` in `%ProgramFiles%\Switchy\` (see below).

### Manual install

1. Download `switchy.exe` and `switchy.ini` from the [Releases] page.
1. Place them in the same folder.
1. Configure `switchy.ini` (see below).
1. Run `switchy.exe`.

### Controls

* **Caps Lock** (default): Switch between Layout 1 and Layout 2.
* **Shift + Caps Lock**: Toggle actual Caps Lock state (upper case mode).
* **Alt + Caps Lock**: Enable/Disable Switchy temporarily.
* **Ctrl + Caps Lock** (when `ConvertWithCtrl=1`):
  Select text in the focused field, then press this chord
  to **re-translate** the selection as if it had been typed on the other layout,
  paste the result, and switch to that layout.
  (Use explicit selection; there is no keystroke buffer.)
* `SmartCaps=1`: plain **Caps Lock** runs a synthetic copy;
  conversion runs only if the Unicode clipboard **changes**
  after that copy (so an old clip is not mistaken for a selection).
  Layout always switches. With **Ctrl** held,
  the layout switches even when nothing is selected.
  Clipboard is backed up and restored;
  very large clips (over ~1M characters) skip conversion and only switch layout.

> [!NOTE]  
> If you changed the `SwitchKey` in the config,
> replace "Caps Lock" with your chosen key in the instructions above.

### Auto-start

The **MSI installer** registers autostart automatically
(`HKCU\Software\Microsoft\Windows\CurrentVersion\Run`).

For a **manual install**, add Switchy to the startup folder:

1. Press **Win+R**, type `shell:startup`, and press Enter.
1. Create a shortcut to `switchy.exe` in that folder.

> [!NOTE]  
> For keyboard switching to work inside programs
> running as Administrator (e.g., Task Manager, RegEdit),
> Switchy must also be run as Administrator.  
> You can set this up via Windows Task Scheduler.

## Configuration (`switchy.ini`)

The program looks for `switchy.ini` in the same directory as the executable.

> [!TIP]
> If you leave `Layout1` or `Layout2` empty (or delete the lines), Switchy
> will automatically use your system's default keyboard layouts for them.

```ini
[Settings]
; Layout codes
; 00000409 = English (US)
; 00000419 = Russian
; 00000422 = Ukrainian
; 00000407 = German
; 0000040C = French
; Leave empty for auto-detection of system layouts.
Layout1=00000409
Layout2=00000419

; The Virtual Key Code for switching (Decimal).
; 20 = Caps Lock (Default)
; 19 = Pause/Break
; 45 = Insert
; 112 = F1
SwitchKey=20

; 1 = Ctrl + SwitchKey converts selected text (see above). 0 = Ctrl+key does nothing special.
ConvertWithCtrl=1

; 1 = smart Caps (see above). 0 = plain Caps only toggles layout.
SmartCaps=0

; If the normal layout request fails: 0 = none, 1 = Alt+Shift cycle, 2 = Ctrl+Shift cycle,
; 3 = Win+Space (language bar). Your Windows "switch input language" hotkey may differ.
FallbackCycleHotkey=0

; Timeout (ms) for the SendMessageTimeout layout-switch request.
; Lower values reduce first-letter lag on fast typing. Range: 10-2000.
SwitchTimeoutMs=80

; Suppress layout switch for fullscreen / borderless foreground windows (games).
; Bit flags: 0 = off, 1 = fullscreen (default), 2 = borderless (no title bar), 3 = both.
FullScreenExcludeSwitch=1

; Same flags for text conversion (Ctrl+SwitchKey / SmartCaps).
FullScreenExcludeConvert=1
```

### Optional: exclude processes

`ExcludeSwitch` disables layout switching;  
`ExcludeConvert` disables the Ctrl+SwitchKey conversion.  
Listing an exe in both disables both behaviors for that process.

The **value** controls the scope of suppression:

* _(empty)_ or `1` - Suppress only when this app's window is active (default)
* `0` - Suppress whenever this process is running anywhere (global)

```ini
[ExcludeSwitch]
; Suppress layout switch when the app window is active (default per-window mode):
SomeApp.exe=
; Same, explicit:
SomeApp.exe=1
; Suppress layout switch whenever the app is running, even in background:
SomeApp.exe=0

[ExcludeConvert]
SomeApp.exe=
```

### Limitations

* **Conversion** covers typical letters, digits, and basic Shift/AltGr mappings
  built from the two configured layouts. Dead keys and exotic compose sequences
  are not promised.
* **Clipboard**: only the Unicode text (`CF_UNICODETEXT`) is restored around
  copy/paste; other clipboard formats may be dropped during the operation.
* **UIPI**: synthetic input may not reach elevated or protected targets unless
  Switchy runs with sufficient integrity.
* **Layouts**: the two layouts are distinguished by **full** `HKL` handles,
  so variants of the same language (e.g. US QWERTY vs Dvorak) are supported.

### How to find Keyboard Layout IDs?

You need the **Input Locale Identifier** (HKL) for your desired languages.

1. **Registry Method (Most Accurate):**
   Open Registry Editor (`regedit`) and navigate to:
   `HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Keyboard Layouts`
   Look through the keys;
   the `Layout Text` value will tell you the language name.
   Copy the folder name (e.g., `00000409`).
1. **Microsoft Docs:**
   You can look up Language Identifiers here:
   [Language Identifier Constants and Strings][language-identifier],
   [Windows Language Code Identifier (LCID) Reference][LCID]
   You usually need the last 4 digits padded with zeros
   (e.g., `0x0409` -> `00000409`).

### How to find Key Codes?

If you want to use a key other than Caps Lock, you need its
[Microsoft Virtual-Key Codes][virtual-key-codes].

**Example**: If you want to use **Right Alt**, the constant is `VK_RMENU`,
the value is `0xA5` (Hex), which is `165` in Decimal.

## Building from source

This fork uses standard C API and does not require Visual Studio.
You can build it using **GCC (MinGW)**.

```bash
make          # clean -> test -> build -> msi
make build    # exe only
make test     # unit tests
make msi      # MSI installer (requires WiX 7: dotnet tool install --global wix)
# or manually:
gcc switchy.c charmap.c -o switchy.exe -mwindows -O2 -std=c99 -luser32 -lkernel32
```

<!-- Links -->
[Releases]: https://github.com/WoozyMasta/Switchy/releases/latest
[switchy]: switchy.jpg
[language-identifier]: https://learn.microsoft.com/en-us/windows/win32/intl/language-identifier-constants-and-strings
[LCID]: https://winprotocoldoc.z19.web.core.windows.net/MS-LCID/%5bMS-LCID%5d-210625.pdf
[virtual-key-codes]: https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
