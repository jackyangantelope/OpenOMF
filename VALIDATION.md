# Release validation

| Check | Result |
|---|---|
| Physical baseline, November 9, 2026 | User confirmed four to five rounds without a freeze, average 59.91 FPS and normal audio. The supplied complete log windows had zero PCM padding. This was a bounded run of `0.1.2-dev03-filediag`. |
| Fruit Jam build | Independent clone built with pinned Pico SDK and Arm GNU Toolchain 14.2.Rel1. Flash 931,392 B and main SRAM 456,816 B (92.94% of the 480 KiB region) at link time. Core0 has a separate 32 KiB stack. |
| Host input | SDL virtual gamepad axes, D-pad, punch, kick, release and disconnect passed under ASan/UBSan. |
| Source audit | Passed blocking checks. No game resources or compiled binaries tracked. |
| Fruit Jam hardware | Gameplay, audio and long-run behavior are pending physical testing. The public build uses Fruit Jam wiring, SDK boot stage and a single USB keyboard host port. |

Both public builds produced identical BIN and UF2. UF2 SHA-256:
`644e9fe7ee8b477ec2f6cbc5ca605f30815dcbb22cd7178228699b7aca976e16`.
Compiler path mapping keeps build directory paths out of the firmware.
Only this validation record was added after the independent build.
