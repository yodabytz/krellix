#include "../windows/PlaybackPcm.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <atomic>
#include <array>
#include <cstddef>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unistd.h>

namespace {
constexpr auto kSystem = kAudioObjectSystemObject;
constexpr auto kGlobal = kAudioObjectPropertyScopeGlobal;
constexpr auto kMain = kAudioObjectPropertyElementMain;

template<class Value>
bool readProperty(AudioObjectID object, AudioObjectPropertySelector selector, Value &value)
{
    const AudioObjectPropertyAddress address{selector, kGlobal, kMain};
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr
        && size == sizeof(value);
}

std::string utf8(CFStringRef value)
{
    if (!value) return {};
    std::array<char, 1024> bytes{};
    if (!CFStringGetCString(value, bytes.data(), bytes.size(), kCFStringEncodingUTF8)) return {};
    return bytes.data();
}

std::string stringProperty(AudioObjectID object, AudioObjectPropertySelector selector)
{
    CFStringRef value = nullptr;
    if (!readProperty(object, selector, value) || !value) return {};
    const auto result = utf8(value);
    CFRelease(value);
    return result;
}

struct Device {
    AudioDeviceID id = kAudioObjectUnknown;
    std::string name;
    std::string uid;
    unsigned channelCount = 0;
    unsigned rate = 0;
};

unsigned channels(AudioDeviceID device, AudioObjectPropertyScope scope)
{
    const AudioObjectPropertyAddress address{kAudioDevicePropertyStreamConfiguration, scope, kMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &address, 0, nullptr, &size) != noErr
        || size < offsetof(AudioBufferList, mBuffers) || size > 65536) return 0;
    const auto freeBuffer = [](void *data) { std::free(data); };
    const std::unique_ptr<void, decltype(freeBuffer)> raw(std::malloc(size), freeBuffer);
    if (!raw || AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, raw.get()) != noErr
        || size < offsetof(AudioBufferList, mBuffers)) return 0;
    const auto *list = static_cast<const AudioBufferList *>(raw.get());
    if (list->mNumberBuffers > (size - offsetof(AudioBufferList, mBuffers)) / sizeof(AudioBuffer)) return 0;
    unsigned count = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) {
        if (list->mBuffers[i].mNumberChannels > 32 - count) return 0;
        count += list->mBuffers[i].mNumberChannels;
    }
    return count;
}

std::vector<Device> devices()
{
    const AudioObjectPropertyAddress address{kAudioHardwarePropertyDevices, kGlobal, kMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kSystem, &address, 0, nullptr, &size) != noErr
        || size > 65536 || size % sizeof(AudioDeviceID)) return {};
    std::vector<AudioDeviceID> ids(size / sizeof(AudioDeviceID));
    if (AudioObjectGetPropertyData(kSystem, &address, 0, nullptr, &size, ids.data()) != noErr) return {};
    ids.resize(size / sizeof(AudioDeviceID));
    std::vector<Device> result;
    for (const auto id : ids) {
        UInt32 transport = 0;
        if (!readProperty(id, kAudioDevicePropertyTransportType, transport)
            || transport != kAudioDeviceTransportTypeVirtual) continue;
        const auto name = stringProperty(id, kAudioObjectPropertyName);
        // These are playback loopbacks. Never select a physical input or an aggregate microphone.
        if (name != "TapRecord" && name != "BlackHole 2ch") continue;
        const auto count = channels(id, kAudioObjectPropertyScopeInput);
        const auto uid = stringProperty(id, kAudioDevicePropertyDeviceUID);
        Float64 rate = 0;
        if (uid.empty() || !count || !channels(id, kAudioObjectPropertyScopeOutput)
            || !readProperty(id, kAudioDevicePropertyNominalSampleRate, rate)
            || !std::isfinite(rate) || rate < 8000 || rate > 192000) continue;
        result.push_back({id, name, uid, count, static_cast<unsigned>(std::lround(rate))});
    }
    return result;
}

struct Capture {
    static constexpr unsigned Capacity = 65536;
    explicit Capture(unsigned channels) : channelCount(channels) {}
    const unsigned channelCount;
    std::array<std::int16_t, Capacity> ring{};
    std::atomic<unsigned> received{0};
    std::atomic<unsigned> consumed{0};
    std::atomic<bool> running{true};
    std::atomic<OSStatus> error{noErr};
};

void captureBuffer(void *context, AudioQueueRef queue, AudioQueueBufferRef buffer,
                   const AudioTimeStamp *, UInt32, const AudioStreamPacketDescription *)
{
    auto &capture = *static_cast<Capture *>(context);
    if (!capture.running.load(std::memory_order_acquire)) return;
    const auto *samples = static_cast<const std::int16_t *>(buffer->mAudioData);
    const auto frames = std::min(buffer->mAudioDataByteSize, buffer->mAudioDataBytesCapacity)
        / (sizeof(*samples) * capture.channelCount);
    const unsigned head = capture.received.load(std::memory_order_relaxed);
    const unsigned free = Capture::Capacity - (head - capture.consumed.load(std::memory_order_acquire));
    const auto count = std::min<std::size_t>(frames, free);
    for (std::size_t i = 0; i < count; ++i) {
        int total = 0;
        for (unsigned channel = 0; channel < capture.channelCount; ++channel)
            total += samples[i * capture.channelCount + channel];
        capture.ring[(head + i) % Capture::Capacity] = static_cast<std::int16_t>(
            total / static_cast<int>(capture.channelCount));
    }
    capture.received.store(head + static_cast<unsigned>(count), std::memory_order_release);
    const auto error = AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    if (error != noErr) capture.error.store(error, std::memory_order_relaxed);
}

bool writePcm(const std::vector<std::int16_t> &samples)
{
    auto *data = reinterpret_cast<const unsigned char *>(samples.data());
    auto remaining = samples.size() * sizeof(samples.front());
    while (remaining) {
        const auto written = write(STDOUT_FILENO, data, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

int startCapture(const Device &device, pid_t parent)
{
    Capture capture(device.channelCount);
    AudioStreamBasicDescription format{};
    format.mSampleRate = device.rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = device.channelCount;
    format.mBitsPerChannel = 16;
    format.mBytesPerPacket = format.mBytesPerFrame = 2 * device.channelCount;
    AudioQueueRef queue = nullptr;
    auto status = AudioQueueNewInput(&format, captureBuffer, &capture, nullptr, nullptr, 0, &queue);
    if (status != noErr) {
        std::fprintf(stderr, "Cannot create Core Audio capture (%d)\n", status);
        return 1;
    }
    struct StopQueue {
        Capture &capture;
        AudioQueueRef queue;
        ~StopQueue()
        {
            capture.running.store(false, std::memory_order_release);
            AudioQueueStop(queue, true);
            AudioQueueDispose(queue, true);
        }
    } stop{capture, queue};
    const CFStringRef uid = CFStringCreateWithCString(kCFAllocatorDefault, device.uid.c_str(), kCFStringEncodingUTF8);
    if (!uid) return 1;
    status = AudioQueueSetProperty(queue, kAudioQueueProperty_CurrentDevice, &uid, sizeof(uid));
    CFRelease(uid);
    CFStringRef actual = nullptr;
    UInt32 size = sizeof(actual);
    if (status != noErr || AudioQueueGetProperty(queue, kAudioQueueProperty_CurrentDevice, &actual, &size) != noErr
        || utf8(actual) != device.uid) {
        if (actual) CFRelease(actual);
        std::fprintf(stderr, "Cannot select the virtual playback device\n");
        return 1;
    }
    CFRelease(actual);
    for (int i = 0; i < 3; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        if (AudioQueueAllocateBuffer(queue, (device.rate / 50) * format.mBytesPerFrame, &buffer) != noErr
            || AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr) != noErr) {
            std::fprintf(stderr, "Cannot allocate Core Audio capture buffers\n");
            return 1;
        }
    }
    status = AudioQueueStart(queue, nullptr);
    if (status != noErr) {
        std::fprintf(stderr, "Cannot start virtual audio capture (%d)\n", status);
        return 1;
    }
    std::fprintf(stderr, "Route playback to %s\n", device.name.c_str());
    SpectrumWindows::Converter converter({device.rate, 1, 16, 2, false});
    std::array<std::int16_t, 4096> chunk{};
    std::vector<std::int16_t> samples;
    samples.reserve(8192);
    bool havePlayback = false;
    unsigned tail = 0;
    while (getppid() == parent && capture.error.load(std::memory_order_relaxed) == noErr) {
        const auto count = std::min<std::size_t>(chunk.size(),
            capture.received.load(std::memory_order_acquire) - tail);
        if (!count) {
            usleep(5000);
            continue;
        }
        for (std::size_t i = 0; i < count; ++i)
            chunk[i] = capture.ring[(tail + i) % Capture::Capacity];
        tail += static_cast<unsigned>(count);
        capture.consumed.store(tail, std::memory_order_release);
        samples.clear();
        converter.append(reinterpret_cast<const unsigned char *>(chunk.data()),
                         static_cast<unsigned>(count), samples);
        if (!havePlayback)
            havePlayback = std::any_of(samples.begin(), samples.end(), [](auto v) { return std::abs(v) >= 32; });
        if (havePlayback && !writePcm(samples)) return 0;
    }
    return capture.error.load(std::memory_order_relaxed) == noErr ? 0 : 1;
}
}

int main(int argc, char **argv)
{
    const auto available = devices();
    if (argc == 2 && std::string(argv[1]) == "--list") {
        for (const auto &device : available)
            std::printf("%s\t%s\t%u\n", device.name.c_str(), device.uid.c_str(), device.rate);
        return 0;
    }
    if (argc != 2 && argc != 3) return 2;
    char *end = nullptr;
    const long parent = std::strtol(argv[1], &end, 10);
    if (!end || *end || parent <= 1 || parent != getppid()) return 2;
    const std::string name = argc == 3 ? argv[2] : "";
    auto pick = std::find_if(available.begin(), available.end(), [&name](const auto &device) {
        return name.empty() ? device.name == "TapRecord" : device.name == name || device.uid == name;
    });
    if (pick == available.end() && name.empty() && !available.empty()) pick = available.begin();
    if (pick == available.end()) {
        std::fprintf(stderr, "No supported virtual playback device found\n");
        return 1;
    }
    std::signal(SIGPIPE, SIG_IGN);
    return startCapture(*pick, static_cast<pid_t>(parent));
}
