#include "PlaybackPcm.h"

#include <array>
#include <cstdlib>
#include <limits>

using namespace SpectrumWindows;

int main()
{
    const PcmFormat signedPcm{48000, 2, 16, 4, false};
    const std::array<unsigned char, 8> stereo{0x00, 0x40, 0x00, 0x00,
                                             0x00, 0x80, 0x00, 0xc0};
    std::vector<std::int16_t> output;
    if (!signedPcm.valid() || PcmFormat{48000, 2, 16, 3, false}.valid()) return EXIT_FAILURE;
    Converter(signedPcm).append(stereo.data(), 2, output);
    if (output != std::vector<std::int16_t>{8192, -24576}) return EXIT_FAILURE;

    const PcmFormat mix{44100, 2, 32, 8, true};
    std::vector<float> input(88200, 0.25f);
    input[0] = std::numeric_limits<float>::quiet_NaN();
    const auto *data = reinterpret_cast<const unsigned char *>(input.data());
    std::vector<std::int16_t> whole, split;
    Converter(mix).append(data, 44100, whole);
    Converter streamed(mix);
    streamed.append(data, 327, split);
    streamed.append(data + 327 * mix.stride, 44100 - 327, split);
    if (whole != split || whole.size() != 47999 || whole.front() != 4096
        || whole.back() != 8192) return EXIT_FAILURE;
    streamed.reset();
    output.clear();
    streamed.append(nullptr, 200, output);
    if (output.empty() || std::any_of(output.begin(), output.end(), [](auto v) { return v != 0; }))
        return EXIT_FAILURE;

    const PcmFormat pcm24{48000, 1, 24, 3, false};
    const std::array<unsigned char, 3> negative{0x00, 0x00, 0x80};
    output.clear();
    Converter(pcm24).append(negative.data(), 1, output);
    if (output != std::vector<std::int16_t>{-32768}) return EXIT_FAILURE;
}
