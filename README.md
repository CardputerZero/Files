# CardputerZero Files

File browser and preview app for M5Stack CardputerZero.

## Features

- Browse folders with animated file menu and long-name scrolling
- Copy, cut, paste, rename, delete, and inspect file metadata
- Preview text files with CJK-capable Noto Sans fonts and scroll progress
- Preview images with pan, zoom, reset, fullscreen, and GIF support
- Play audio files with seek and 1x/2x/5x speed controls
- Play video files fullscreen through `ffmpeg` on the device framebuffer
- Fall back to an info preview for unsupported file types

## Dependencies

Run the bootstrap script once after cloning this repository:

```bash
./bootstrap.sh
```

It creates `.venv/`, installs the Python build tools, fetches dependencies from
`repos.json`, and keeps all third-party source under `dependencies/`.

System packages expected by the build:

- CMake and a C/C++ compiler
- SDL2 development files for `FILES_USE_SDL=ON`
- `python3-venv` so `bootstrap.sh` can create `.venv/`
- `aarch64-linux-gnu-gcc/g++` for cross-building the CardputerZero package
- `ffmpeg` on the target device for video preview playback

On macOS, install the desktop build tools with Homebrew:

```bash
brew install cmake pkg-config sdl2
```

macOS supports the SDL desktop build only. Device framebuffer and Debian
packaging are Linux/CardputerZero targets.

Project dependencies pulled from `repos.json`:

- `lvgl`
- `spdlog`
- `smooth_ui_toolkit`
- `miniaudio`

Image asset conversion runs during build and uses LVGL's Python converter. If
the converter dependencies are missing, install them in your Python environment:

```bash
./.venv/bin/python -m pip install pypng lz4 Pillow
```

Font conversion is not part of the normal build. If fonts are regenerated with
`src/assets/convert_fonts.py`, install `lv_font_conv` separately and keep it
available in `PATH`.

## Build

For Linux SDL testing:

```bash
cmake -S . -B build/sdl -DFILES_USE_SDL=ON
cmake --build build/sdl -j8
```

For macOS SDL testing:

```bash
cmake -S . -B build/macos-sdl -DFILES_USE_SDL=ON
cmake --build build/macos-sdl -j8
```

For CardputerZero framebuffer build:

```bash
cmake -S . -B build/cp0 -DFILES_USE_SDL=OFF
cmake --build build/cp0 -j8
```

For cross build from x86 Linux with the GNU aarch64 toolchain:

```bash
cmake -S . -B build/cp0 \
  -DFILES_USE_SDL=OFF \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake
cmake --build build/cp0 -j8
```

The output binary is `dist/M5CardputerZero-Files`.

## Usage

Run the SDL build:

```bash
./dist/M5CardputerZero-Files
```

By default, SDL builds start in `$HOME`; packaged CardputerZero launches also
start in `$HOME`. Override the initial folder with:

```bash
FILES_START_DIR=/path/to/folder ./dist/M5CardputerZero-Files
```

On hardware, `F`/`X`/`Z`/`C` are accepted as Up/Down/Left/Right.

Key controls:

- Browser page: Up/Down select, Enter open, Tab action menu, Esc or Left back
- Action menu: Up/Down select, Enter confirm, Esc/Left/Tab close
- Text preview: Up/Down scroll, Esc or Left back
- Image preview: arrows pan, `4` fullscreen, `5` zoom out, `7` zoom in, `8` reset, Esc back
- Audio preview: `5` back 10s, `6` play/pause, `7` forward 10s, `8` speed, Esc/Left back
- Video preview: Space pause/resume, Left/Right seek, Esc back

Video preview uses `ffmpeg` directly on CardputerZero. Audio is sent to the
PulseAudio sink named by `FILES_VIDEO_PULSE_SINK`, or to `default` when unset.

Keyboard input debug logs can be enabled with:

```bash
FILES_KEYBOARD_DEBUG=1 ./dist/M5CardputerZero-Files
```

## Package

Build the cp0/CardputerZero Debian package:

```bash
./packaging/deb/package_deb.sh
```

The package script is device-targeted only. It always configures the framebuffer
build and produces an `arm64` APPLaunch package. The generated package depends
on `ffmpeg` for video preview playback.

The generated package is written to `dist/`:

```text
dist/m5cardputerzero-files_0.1.1_m5stack1_arm64.deb
```
