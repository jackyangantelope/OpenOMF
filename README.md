# OpenOMF for RP2350

OpenOMF 0.8.6 based port for Adafruit Fruit Jam with 8 MiB PSRAM, HDMI
video/audio, SD storage and USB keyboard input. The game runs in offline
mode. The public board uses a single USB host port.

Supply your own One Must Fall 2097 resources. Put the OpenOMF
`resources` directory under `/roms/OPENOMF` on the SD card. The picker
recognizes a dataset containing `resources/openomf.bk`, `SOUNDS.DAT`
and `ENGLISH.DAT`. Supply the complete compatible resource set.
See the upstream [OpenOMF 0.8.6 release](https://github.com/omf2097/openomf/releases/tag/0.8.6).

The tested port baseline was `0.1.2-dev03-filediag`. On November 9, 2026
the user confirmed four to five rounds without a freeze, average 59.91
FPS and normal audio. Public Fruit Jam gameplay and long runs are
pending physical testing.

## Build

Install Git, CMake 3.20+, Ninja, Python 3 and Arm GNU Toolchain 14.2.Rel1.
Pico SDK 2.3.0, TinyUSB and Pico-PIO-USB are pinned in
`dependencies.lock.json`.

```powershell
./scripts/fetch-dependencies.ps1 -Destination <outside-source-directory>
./scripts/build.ps1 -PicoSdkPath <pico-sdk> -PicoPioUsbPath <Pico-PIO-USB> `
  -ToolchainPath <arm-toolchain> -NinjaPath <ninja.exe>
```

For an offline build pass `-PioasmDir` and `-PicotoolDir` using local
SDK host tools. The output is `build/fruit-jam/picoOpenOmfNative.uf2`.
The application uses a compatible 16 MiB flash loader at `0x10080000`.

For the host input regression, install GCC, pkg-config and SDL2 development
headers on Linux or WSL, then run `bash scripts/test-host.sh`.

See [THIRD_PARTY.md](THIRD_PARTY.md) and [VALIDATION.md](VALIDATION.md)
for license terms and verification scope.
