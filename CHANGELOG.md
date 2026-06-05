<!-- markdownlint-disable MD024 -->
# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog][],
and this project adheres to [Semantic Versioning][].

<!--
## Unreleased

### Added
### Changed
### Removed
-->

## [2.3.0] - 2026-06-05

### Added

* `FullScreenExcludeSwitch` and `FullScreenExcludeConvert` INI bit flags
  for automatically suppress layout switching and/or conversion
  for fullscreen or borderless-window apps without listing them by name.
* Per-entry scope for `[ExcludeSwitch]` and `[ExcludeConvert]` entries:
  the INI value now controls whether suppression is scoped to the
  active window or to any running instance
  of the process (`0`, global mode).

### Changed

* `[ExcludeSwitch]` and `[ExcludeConvert]` previously checked only the
  foreground window;
  the new `=0` syntax opts individual entries into global suppression
  (while keeping per-window as the default).
  Existing configs (`exe.name=` with no value) are unaffected.
* Shipped `switchy.ini` now has `Layout1` and `Layout2` empty by default,
  so auto-detection of system layouts is used out of the box
  instead of hardcoded codes.

[2.3.0]: https://github.com/WoozyMasta/Switchy/compare/2.2.0...2.3.0

## [2.2.0] - 2026-06-05

### Added

* New INI option `SwitchTimeoutMs` (default `80`):
  controls the `SendMessageTimeout` deadline for layout-switch requests;
  lower values reduce first-letter lag on fast typing.
* MSI installer WiX: installs to `%ProgramFiles%\Switchy\`
  and registers autostart via `HKCU\...\Run`.
* Unit tests for the character-mapping module (`tests/test_charmap.c`).

### Fixed

* ASCII punctuation shared between layouts (`.` `,` and similar)
  no longer gets remapped to a different symbol
  when converting mixed-layout text in the reverse direction.

### Changed

* `SwitchTimeoutMs` default reduced from 200 ms to 80 ms.

[2.2.0]: https://github.com/WoozyMasta/Switchy/compare/2.1.0...2.2.0

## [2.1.0] - 2026-04-07

### Added

* Optional Ctrl plus switch key (`ConvertWithCtrl`):
  turn the current selection into the other configured layout,
  replace it, and switch input to that layout.
* Letter-by-letter conversion between the two layouts;
  characters with no mapping are left as they are.
* Sections `ExcludeSwitch` and `ExcludeConvert` in the config
  to disable switching or conversion in specific apps (by executable name).
* Optional `SmartCaps`:
  the switch key alone can run the same conversion flow without Ctrl;
  conversion runs only when the clipboard text actually changes after copy,
  so a plain layout switch does not reuse an old clip;
  oversized clipboard text only triggers a layout switch.
* Optional `FallbackCycleHotkey`:
  if the layout still does not apply,
  press the system "next input language" shortcut once (configurable variant).

### Changed

* Switching follows the focused control
  (including typical modal dialogs such as Save As),
  not only the top-level window.
* The two layouts are told apart by the full keyboard layout identity,
  so two variants under one Windows language (for example QWERTY and Dvorak)
  are handled correctly.
* The configuration file and paths are read as Unicode end to end.

## [2.0.1] - 2025-12-18

### Added

* If a specific layout (`Layout1` or `Layout2`) is missing from the
  configuration file or fails to load, the application now individually
  falls back to the system's default layouts instead of failing.

### Fixed

* Fixed an issue where the `.ini` file wasn't found when the app was
  launched via a shortcut (e.g., from `shell:startup`).
  It now correctly looks for the config next to the actual `.exe` file.

[2.0.1]: https://github.com/WoozyMasta/Switchy/compare/2.0.0...2.0.1

## [2.0.0] - 2025-12-18

### Added

* Configuration file `switchy.ini` support. Users can now define exactly
  which two layouts to toggle between (e.g., US and RU),
  ignoring other installed system layouts.
* Custom hotkey - ability to map the switch action to any Virtual Key code
  via `switchy.ini` (defaults to Caps Lock).
* Silent switching - direct window message sending
  (`WM_INPUTLANGCHANGEREQUEST`), effectively making the "nopopup"
  behavior the default and only mode.
* Support for GCC/MinGW compilation.

### Changed

* Refactored the codebase to standard C99, removing MSVC-specific dependencies
* The switching logic no longer cycles through the Windows language list.
  It now strictly swaps between `Layout1` and `Layout2` defined in the config.
* Updated the license file to reflect the new maintainer and year.

### Removed

* `.sln` and `.vcxproj` files. The project no longer requires MSVC to build.
* Support for command-line arguments (e.g., `nopopup`),
  as they are no longer needed with the new architecture.

[2.0.0]: https://github.com/WoozyMasta/Switchy/compare/1.4.3...2.0.0

<!--links-->
[Keep a Changelog]: https://keepachangelog.com/en/1.1.0/
[Semantic Versioning]: https://semver.org/spec/v2.0.0.html
