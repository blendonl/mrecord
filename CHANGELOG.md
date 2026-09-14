# Changelog

All notable changes to mrecord are documented here. This project adheres to
[Semantic Versioning](https://semver.org/) (pre-1.0: minor = features + fixes).
Sections after 0.1.0 are generated from commit messages; see *Releases* in the
README.

## 0.1.0

First release.

### Added

- **Record a region, a window or a monitor to MP4.** `--select` shows a dimmed
  overlay to drag a rectangle on (a single click takes the window under the
  cursor), `--window` takes the focused window, `--monitor [N]` a whole
  display and `--rect X,Y,W,H` an exact rectangle. `--list` prints the
  monitors in the same `X,Y,W,H` shape.
- **One hotkey for start and stop.** `--toggle` stops the recording that is
  running, or starts one; `--stop` only stops. Either waits for the file to be
  finalized and prints its path. `--duration`, `Ctrl+C`, `taskkill` without
  `/F` and signing out all finalize the file too.
- **H.264 at a constant frame rate** through Media Foundation, hardware
  encoders allowed. `--fps` and `--bitrate` override the defaults, which scale
  with the region.
- **System sound** with `--audio`: a WASAPI loopback encoded as AAC, with
  silence filled in while nothing plays so sound and picture stay in step.
- **The mouse pointer** with `--cursor`, and a red frame around the recorded
  region that is excluded from capture (`--no-border` hides it).
- **Recovery from lost duplication.** A mode change, UAC prompt or lock screen
  pauses capture and recording resumes when the desktop comes back.
- **HDR monitors** record as SDR instead of failing.
