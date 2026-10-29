// Walk-up songs: shared between walkup.cpp (hooks, song choice, the voice) and walkup_decode.cpp (decoders).
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace walkup {

// a song cut for playing: interleaved stereo float at audio::kRate
struct Clip {
    std::vector<float> pcm;
    size_t fade_from = 0;  // frame where the closing fade starts
};

// decodes [start, start + length) seconds of the file, any length the file has if length <= 0. WAV, MP3, M4A/AAC,
// WMA and FLAC go through Media Foundation, OGG through stb_vorbis. Empty pcm and a reason in error on failure.
Clip Decode(const std::filesystem::path& file, double start, double length, std::string& error);

// ballpark PA sound (walkup_reverb.cpp): PA tone, stand echoes and a hall tail appended to the clip
void Stadium(Clip& clip);

}  // namespace walkup
