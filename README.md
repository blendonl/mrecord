# mrecord

A **screen recorder for the Windows command line**. Point it at a region, a
window or a monitor and it writes an H.264 MP4, with system sound if you ask.
One binary, no installer required, nothing running in the background between
recordings.

```
mrecord                      drag a rectangle, record until `mrecord --stop`
mrecord --monitor 1 -D 30    record monitor 1 for thirty seconds
mrecord --window --audio     record the focused window with system sound
mrecord --toggle             start, or stop the one that is running
```

It runs under plain Explorer or any window manager and needs neither. It is a
natural fit for [mshell](https://github.com/blendonl/mshell), a tiling WM that
replaces `explorer.exe`, where there is no Snipping Tool shortcut and no Game
Bar; companion to [mcapture](https://github.com/blendonl/mcapture) for
screenshots.

## Usage

```
mrecord [region] [options]

Region (pick one; default --select). The region must lie on a single monitor:
  -s, --select          live dimmed overlay: drag a rectangle (WxH label), single click takes the window
                        under the cursor, Enter/release confirms, Esc or right-click cancels; clamped to
                        the monitor the drag started on
  -m, --monitor [N]     monitor N from --list; without N, the monitor under the cursor
  -w, --window          the foreground window's visible frame (region fixed at start)
  -r, --rect X,Y,W,H    explicit rectangle, physical virtual-screen pixels
Control:
  -t, --toggle          if a recording is running, stop it; otherwise start one (one hotkey for both)
      --stop            stop the running recording and exit
  -D, --duration SECS   stop by itself
Options:
  -o, --output PATH     .mp4; default %USERPROFILE%\Videos\Screen Recordings\mrecord-YYYYMMDD-HHMMSS.mp4
      --fps N           default 30 (constant frame rate; repeat the last frame when nothing changed)
      --bitrate KBPS    default derived from region size and fps
  -a, --audio           also record system sound (WASAPI loopback, AAC)
  -c, --cursor          draw the mouse pointer
  -d, --delay SECONDS   wait before starting
      --no-border       no on-screen frame around the recorded region
      --list            print monitors: index, device, x,y,w,h, primary
  -h, --help / -V, --version

stdout: the written path when the recording finishes.
Exit: 0 ok, 1 failure, 2 bad usage, 3 cancelled / nothing to stop.
```

`--list` prints one monitor per line, ordered left to right and then top to
bottom, with the rectangle in the same `X,Y,W,H` shape `--rect` takes:

```
0 \\.\DISPLAY1 0,0,3840,2160 primary
1 \\.\DISPLAY2 3840,0,3840,2160
```

### Stopping

A recording runs until one of these happens, and every one of them finalizes
the file so it plays:

- `mrecord --stop` (or `mrecord --toggle`) from anywhere in the same session.
  It waits for the file to be written and prints its path.
- `--duration` runs out.
- `Ctrl+C` in the console it was started from.
- `taskkill /IM mrecord.exe` without `/F`, or signing out.

Only one recording runs per session; starting a second one fails with exit
code 1 and says so.

### Under mshell

Bind it like any other program, one key for both start and stop. Pressing
`r` again stops the recording `r` started:

```lua
mshell.keys.submap("capture", {
    r = { function() mshell.exec("mrecord.exe", "--toggle --select") end,  desc = "record region" },
    v = { function() mshell.exec("mrecord.exe", "--toggle --monitor") end, desc = "record monitor" },
})
```

`mrecord` is a GUI-subsystem program, so launching it from a hotkey does not
flash a console window. The flip side is that `cmd.exe` does not wait for it:
use `start /wait mrecord ...` there, or pipe it in PowerShell
(`mrecord --list | Out-Host`) when you want the prompt to come back after it.

## How it works

- **Capture** is DXGI Desktop Duplication on the monitor the region lies on,
  asking for 8-bit BGRA frames so an HDR monitor records as SDR rather than
  failing. When Windows takes the duplication away (a mode change, a UAC
  prompt, the lock screen) it is recreated and recording carries on.
- **Pacing** is constant frame rate against the performance counter: every
  tick writes the newest frame, repeating the last one when nothing on screen
  changed. If encoding falls behind, frames are skipped rather than letting the
  video drift from real time.
- **Encoding** is Media Foundation's H.264 encoder into MP4, with hardware
  encoders allowed, so an NVIDIA, AMD or Intel GPU does the work when there is
  one.
- **Audio** is a WASAPI loopback of the default output device, converted to
  16-bit stereo and encoded as AAC. Loopback delivers nothing while nothing
  plays, so the gaps are filled with silence to keep sound and picture in
  step.
- **The red frame** around the region is four click-through windows marked
  `WDA_EXCLUDEFROMCAPTURE`, so it never appears in the video. It sits outside
  the region where there is room and inside it at a monitor edge.

## Limitations

- A region must lie on one monitor; `--select` clamps the drag to the monitor
  it started on, and `--rect` refuses a rectangle that spans two.
- HDR content is recorded as SDR.
- Windows that opt out of capture (DRM video, some password managers) are
  black, as in every other recorder.
- Only system sound is recorded, not a microphone.

Logs go to `%LOCALAPPDATA%\mrecord\mrecord.log`.

## Build

Cross-compiled from Linux with **mingw-w64**:

```sh
sudo apt install gcc-mingw-w64-x86-64
make            # -> mrecord.exe
make test       # host-side unit tests
make dist       # -> dist/mrecord-<version>-win64.zip
```

`make test` covers everything with no Windows in it: argument parsing, region
and monitor geometry, output naming, frame pacing and the audio and colour
conversions. What needs a real desktop is in [MANUAL-TESTS.md](MANUAL-TESTS.md).

## Releases

Every pull request merged into `main` is a release. While the PR is open, CI
works out the next version from its
[Conventional Commits](https://www.conventionalcommits.org/) and pushes a
`chore(release): vX.Y.Z` commit to the branch that sets `VERSION` in the
Makefile and writes the matching section of `CHANGELOG.md`. When the PR merges,
CI tags that version and publishes the zip with that section as its notes.

Before 1.0, `feat`, `fix` and breaking changes bump the minor version. From
1.0, breaking changes bump the major, `feat` the minor and `fix` the patch.
Anything else bumps the patch.

## Install

In PowerShell:

```powershell
irm https://raw.githubusercontent.com/blendonl/mrecord/main/install.ps1 | iex
```

That downloads the latest release into `%LOCALAPPDATA%\Programs\mrecord` and
adds the folder to your user `PATH`. Run it again to upgrade; a recording
running from that folder is stopped first. To pin a version, or install
somewhere else:

```powershell
& ([scriptblock]::Create((irm https://raw.githubusercontent.com/blendonl/mrecord/main/install.ps1))) -Version 0.1.0 -InstallDir C:\Tools\mrecord
```

## License

MIT. See [LICENSE](LICENSE).
