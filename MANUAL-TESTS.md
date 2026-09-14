# Manual test checklist

`make test` covers the logic with no Windows in it: argument parsing, region
and monitor geometry, output naming, frame pacing, and the audio and colour
conversions. Everything below needs a real Windows desktop, because it
involves capture, encoding, audio devices or windows.

`%LOCALAPPDATA%\mrecord\mrecord.log` is written on every recording and is the
first place to look. To check a file without opening a player, PowerShell's
shell properties are enough:

```powershell
$f = (New-Object -ComObject Shell.Application).Namespace((Get-Item a.mp4).DirectoryName).ParseName('a.mp4')
'System.Media.Duration','System.Video.FrameWidth','System.Video.FrameHeight','System.Video.FrameRate','System.Audio.SampleRate' |
    ForEach-Object { "$_ = $($f.ExtendedProperty($_))" }
```

## Command line

| # | Test | Expected |
|---|------|----------|
| 1 | `mrecord --version` | Prints `mrecord 0.1.0`, exit 0. |
| 2 | `mrecord --list` | One line per monitor, left to right, `N \\.\DISPLAYn X,Y,W,H`, `primary` on one of them. |
| 3 | `mrecord --bogus` | `unknown argument`, a pointer to `--help`, exit 2. |
| 4 | `mrecord --stop` with nothing recording | `no recording is running`, exit 3. |
| 5 | `mrecord --rect 0,0,99999,10` | Refused: does not lie on a single monitor, exit 2. |

## Recording

| # | Test | Expected |
|---|------|----------|
| 6 | `mrecord --rect 100,100,640,480 -D 3 -o a.mp4` | Prints the full path of `a.mp4`, exit 0. Duration 3.0 s, 640 × 480, 30 fps, H.264. Plays in Media Player, right way up. |
| 7 | `mrecord --monitor 1 -D 3` | The second monitor at its full resolution, saved under `Videos\Screen Recordings` with a timestamped name. |
| 8 | `mrecord --rect 101,101,641,481 -D 1` | 640 × 480: odd sizes round down to even. |
| 9 | `mrecord --monitor 0 --fps 60 -D 3` on a 4K display | About 180 frames in the log's `stopping after` line. |
| 10 | Record a whole monitor with the border on | No red at the edges of the video: the frame is excluded from capture. |
| 11 | `mrecord --window -D 3` with a window focused | Exactly that window's visible frame, without the invisible resize border. |
| 12 | `mrecord -D 5 --audio` while something plays | An AAC stream at 44.1 or 48 kHz, stereo, in time with the picture. |
| 13 | `mrecord -D 5 --audio` with nothing playing | Still an audio stream, silent, the same length as the video. |
| 14 | `mrecord --rect ... -c -D 2` around the pointer | The pointer is in the video at the right place. |
| 15 | `mrecord --rect ... -d 2 -D 1` | Recording starts two seconds later. |
| 16 | Start a recording, lock the screen, unlock | The log shows the duplication recreated; the file keeps going and plays. |
| 17 | Start a recording, change that monitor's resolution | Recording stops with an error if the region no longer fits, and the file up to that point plays. |

## Stopping

| # | Test | Expected |
|---|------|----------|
| 18 | Start one, then `mrecord --stop` | The recorder exits 0 and prints the path; `--stop` prints the same path and exits 0. |
| 19 | Start one, then start another | The second says a recording is already running, exit 1. |
| 20 | `mrecord --toggle` twice | The first starts a recording, the second stops it. |
| 21 | Start one from a console, press `Ctrl+C` | The file is finalized and plays. |
| 22 | Start one, then `taskkill /IM mrecord.exe` (no `/F`) | Stops cleanly, exit 0, the file plays. With and without `--no-border`. |
| 23 | Start one and sign out | The file is finalized before the session ends. |

## Selecting

| # | Test | Expected |
|---|------|----------|
| 24 | `mrecord -s -D 2`, drag a rectangle | The label shows `W × H` while dragging; the video is that rectangle. |
| 25 | `mrecord -s -D 2`, drag across onto a second monitor | The rectangle stops at the edge of the monitor the drag started on. |
| 26 | `mrecord -s -D 2`, single click on a window | The video is that window's frame. |
| 27 | `mrecord -s`, press `Esc` | `selection cancelled`, exit 3, no file. Right-click does the same. |
| 28 | `mrecord -s`, press `Enter` without dragging | Records the monitor under the pointer. |
| 29 | With swapped mouse buttons | The primary button drags; the other one cancels. |

## Under a window manager

| # | Test | Expected |
|---|------|----------|
| 30 | Under mshell, bind `mrecord.exe --toggle --select` and press it | No console window flashes. The overlay takes the keyboard (Esc works without clicking). mshell's log has no `Managed:` or `Tracking:` line for mrecord's overlay, border or control windows. |
| 31 | Press the binding again during the recording | The recording stops. |
