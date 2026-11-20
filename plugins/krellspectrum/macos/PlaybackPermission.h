#pragma once

enum class PlaybackPermission { Allowed, Waiting, Denied };
PlaybackPermission macPlaybackPermission(bool request);
