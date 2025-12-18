# Switchy

A lightweight utility to toggle between two specific keyboard layouts
using **Caps Lock** (or any other specified key).

Unlike the standard Windows behavior
(which cycles through all installed layouts),
**Switchy** allows you to define exactly **two main layouts**
in a configuration file.  
Pressing the key will instantly toggle between them,
ignoring any other layouts installed on the system.

![switchy](switchy.jpg)

The switching happens silently
(using the direct `WM_INPUTLANGCHANGEREQUEST` message),
so it does not trigger the annoying Windows 10/11 language pop-up menu.

## Usage

1. Download `switchy.exe` and `switchy.ini` from the
   [Releases](https://github.com/WoozyMasta/Switchy/releases/latest) page.
1. Place them in the same folder.
1. Configure `switchy.ini` (see below).
1. Run `switchy.exe`.

### Controls

* **Caps Lock** (default): Switch between Layout 1 and Layout 2.
* **Shift + Caps Lock**: Toggle actual Caps Lock state (upper case mode).
* **Alt + Caps Lock**: Enable/Disable Switchy temporarily.

> [!NOTE]  
> If you changed the `SwitchKey` in the config,
> replace "Caps Lock" with your chosen key in the instructions above.

### Auto-start

To run Switchy automatically:

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
```

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
   [Language Identifier Constants and Strings](https://learn.microsoft.com/en-us/windows/win32/intl/language-identifier-constants-and-strings),
   [Windows Language Code Identifier (LCID) Reference](https://winprotocoldoc.z19.web.core.windows.net/MS-LCID/%5bMS-LCID%5d-210625.pdf)
   You usually need the last 4 digits padded with zeros
   (e.g., `0x0409` -> `00000409`).

### How to find Key Codes?

If you want to use a key other than Caps Lock, you need its
[Microsoft Virtual-Key Codes](https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes).

**Example**: If you want to use **Right Alt**, the constant is `VK_RMENU`,
the value is `0xA5` (Hex), which is `165` in Decimal.

## Building from source

This fork uses standard C API and does not require Visual Studio.
You can build it using **GCC (MinGW)**.

```bash
gcc switchy.c -o switchy.exe -mwindows -O2 -luser32 -lkernel32
```
