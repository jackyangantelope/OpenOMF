# Source attribution

| Component | Origin and license | Included changes |
|---|---|---|
| OpenOMF | [omf2097/openomf](https://github.com/omf2097/openomf) 0.8.6 based snapshot, MIT in `deps/openomf/LICENSE` | Native software renderer and audio backend, fixed resolution and PSRAM paths, reduced runtime dependencies, and preserved file and timing diagnostics. The port snapshot includes local changes. |
| pico_shared | [fhoedemakers/pico_shared](https://github.com/fhoedemakers/pico_shared), GPLv3 with retained component notices | Selected HDMI driver snapshot and SD/FatFs glue. The display and PCM adapters are under `platform/runtime`. |
| SDL2 and ENet headers | SDL2 2.30.0 and ENet 1.3.17 packaged headers with notices and version record in `deps/native-headers` | Header interfaces used by the native adapter. |
| zlib and libpng | zlib 1.3.1 and libpng 1.6.43, their respective notices in `deps/libs` | Selected source builds for PNG decoding and compression. |
| libxmp | [libxmp](https://github.com/libxmp/libxmp) 4.6.0, LGPL-2.1-or-later | Module playback with optional depackers and ProWizard disabled. |
| libConfuse | [libConfuse](https://github.com/libconfuse/libconfuse) 3.3, ISC | Bare metal configuration parsing and the generated lexer. |
| Build dependencies | Public revisions in `dependencies.lock.json` | Pico SDK, TinyUSB and Pico-PIO-USB fetched separately. |

The combined firmware source distribution uses GPLv3. Individual
component terms and copyright notices remain in their respective files.
Platform source marked BSD-3-Clause uses `LICENSE.BSD-3-Clause`.
The game assets are distributed separately by their respective owners.
