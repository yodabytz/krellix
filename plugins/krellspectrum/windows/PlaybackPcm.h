#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace SpectrumWindows {
inline constexpr unsigned kOutputRate = 48000;

struct PcmFormat {
    unsigned rate = 0;
    unsigned channels = 0;
    unsigned bits = 0;
    unsigned stride = 0;
    bool floating = false;

    bool valid() const
    {
        return rate >= 8000 && rate <= 384000 && channels >= 1 && channels <= 32
            && (floating ? bits == 32 : (bits == 8 || bits == 16 || bits == 24 || bits == 32))
            && stride == channels * (bits / 8);
    }

    double sample(const unsigned char *data) const
    {
        if (floating) {
            float value = 0;
            std::memcpy(&value, data, sizeof(value));
            return std::isfinite(value) ? std::clamp(static_cast<double>(value), -1.0, 1.0) : 0.0;
        }
        if (bits == 8)
            return (static_cast<int>(*data) - 128) / 128.0;
        std::uint32_t packed = 0;
        for (unsigned i = 0; i < bits / 8; ++i)
            packed |= static_cast<std::uint32_t>(data[i]) << (8 * i);
        const auto sign = std::int64_t{1} << (bits - 1);
        const auto value = packed >= sign ? static_cast<std::int64_t>(packed) - 2 * sign : packed;
        return static_cast<double>(value) / static_cast<double>(sign);
    }

    double downmix(const unsigned char *frame) const
    {
        double total = 0.0;
        for (unsigned channel = 0; channel < channels; ++channel)
            total += sample(frame + channel * (bits / 8));
        return total / channels;
    }
};

class Converter {
public:
    explicit Converter(PcmFormat format) : m_format(format) {}

    void reset()
    {
        m_havePrevious = false;
        m_next = 0;
    }

    void append(const unsigned char *data, unsigned frames, std::vector<std::int16_t> &out)
    {
        for (unsigned frame = 0; frame < frames; ++frame) {
            const double value = data ? m_format.downmix(data + frame * m_format.stride) : 0.0;
            if (!m_havePrevious) {
                out.push_back(encode(value));
                m_next = m_format.rate;
                m_havePrevious = true;
            } else {
                // Preserve the fractional phase across WASAPI packet boundaries.
                while (m_next <= kOutputRate) {
                    const auto weight = static_cast<double>(m_next) / kOutputRate;
                    out.push_back(encode(m_previous + (value - m_previous) * weight));
                    m_next += m_format.rate;
                }
                m_next -= kOutputRate;
            }
            m_previous = value;
        }
    }

private:
    static std::int16_t encode(double value)
    {
        return static_cast<std::int16_t>(std::clamp(std::lround(value * 32768.0), -32768L, 32767L));
    }

    PcmFormat m_format;
    unsigned m_next = 0;
    double m_previous = 0.0;
    bool m_havePrevious = false;
};
}
