# KrellHello

KrellHello is a local-only example plugin: a text readout and a moving bar
driven by a demo counter. It does not need a network, hardware or credentials.
It runs inside the host application. This source is covered by the Krellix
repository's license; keep the included license with the example archive.

Use the example in its matching Krellix source checkout. If using the ZIP as
a starting point, the folder belongs at `examples/krellhello`, not beside
the installed app. Configure from the root of the Krellix checkout:

```sh
cmake -S . -B build-author -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DKRELLIX_BUILD_EXAMPLES=ON
cmake --build build-author --target krellix krellhello
```

Windows requires the matching UCRT64 toolchain, Qt runtime, and the sensor
helper directory specified by `-DKRELLIX_SENSOR_DIR=/path/to/sensors`. See the
[full authoring guide](https://cerberix.org/extras/krellix/plugins/#build-a-plugin)
for platform-specific prerequisites, launch commands, and packaging notes.
