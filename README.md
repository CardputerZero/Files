# Files

File browser app for M5CardputerZero APPLaunch.

## Build for SDL

```sh
./bootstrap.sh
cmake -S . -B build/sdl -DFILES_USE_SDL=ON
cmake --build build/sdl -j$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
./dist/M5CardputerZero-Files
```

Set `FILES_START_DIR=/path/to/folder` to choose the initial folder.

## Build Debian Package

```sh
./packaging/deb/package_deb.sh
```

The package is written to `dist/m5cardputerzero-files_0.1.1_m5stack1_arm64.deb`.
