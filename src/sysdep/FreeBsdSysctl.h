#pragma once

#include <sys/sysctl.h>

#include <cerrno>
#include <cstddef>
#include <optional>
#include <vector>

namespace FreeBsdSysctl {

template <typename T>
std::optional<T> read(const char *name)
{
    T value{};
    size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || size != sizeof(value))
        return std::nullopt;
    return value;
}

template <typename T>
std::vector<T> readArray(const char *name, size_t maxBytes)
{
    // A process or CPU can appear between the size query and the read.
    for (int attempt = 0; attempt < 3; ++attempt) {
        size_t size = 0;
        if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0
            || size == 0 || size > maxBytes || size % sizeof(T) != 0)
            return {};

        std::vector<T> values(size / sizeof(T));
        if (sysctlbyname(name, values.data(), &size, nullptr, 0) == 0) {
            if (size > values.size() * sizeof(T) || size % sizeof(T) != 0)
                return {};
            values.resize(size / sizeof(T));
            return values;
        }
        if (errno != ENOMEM)
            return {};
    }
    return {};
}

} // namespace FreeBsdSysctl
