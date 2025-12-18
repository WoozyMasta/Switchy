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
