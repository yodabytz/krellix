# KrellSpectrum playback capture on macOS

The helper reads PCM from an existing virtual loopback device using Core
Audio. It supports TapRecord or BlackHole 2ch. It does not install audio
drivers, change the default sound device, or select a physical microphone.
The normal Krellix renderer is unchanged.

1. In Settings > Plugins > KrellSpectrum, enable the local visualizer.
   Select Auto for the backend. Leave Device blank to select an available
   supported loopback, or enter its exact name or Core Audio UID.
2. If macOS asks for Microphone access for Krellix, allow it. macOS uses
   that permission for virtual input, too. Without it the helper cannot
   receive playback.
3. Route the application's playback to the selected virtual device.
   To hear system playback as well, create a Multi-Output Device in
   Audio MIDI Setup that includes the virtual device and your listening
   device, then select that output yourself. Switching back to your
   normal device restores your usual output and volume controls.

Audio is processed in memory. It is not recorded or sent over the network.
Remote windows do not start the helper.
