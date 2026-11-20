#include "PlaybackPcm.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace {
struct ComRelease {
    template<class T> void operator()(T *p) const { p->Release(); }
};
struct TaskFree {
    void operator()(void *p) const { CoTaskMemFree(p); }
};
struct CloseWinHandle {
    void operator()(void *value) const { CloseHandle(value); }
};
template<class T> using ComPtr = std::unique_ptr<T, ComRelease>;
using UniqueHandle = std::unique_ptr<void, CloseWinHandle>;

constexpr PROPERTYKEY kFriendlyName = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14
};
constexpr GUID kPcm = {WAVE_FORMAT_PCM, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
constexpr GUID kFloat = {WAVE_FORMAT_IEEE_FLOAT, 0, 0x10, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};

std::wstring endpointId(IMMDevice *device)
{
    LPWSTR raw = nullptr;
    const HRESULT hr = device->GetId(&raw);
    const std::unique_ptr<wchar_t, TaskFree> id(raw);
    return SUCCEEDED(hr) && id ? std::wstring(id.get()) : std::wstring();
}

bool friendlyNameMatches(IMMDevice *device, const std::wstring &requested)
{
    IPropertyStore *raw = nullptr;
    const HRESULT hr = device->OpenPropertyStore(STGM_READ, &raw);
    const ComPtr<IPropertyStore> props(raw);
    if (FAILED(hr)) return false;
    PROPVARIANT name{};
    const HRESULT value = props->GetValue(kFriendlyName, &name);
    const bool equal = SUCCEEDED(value) && name.vt == VT_LPWSTR
        && name.pwszVal && _wcsicmp(name.pwszVal, requested.c_str()) == 0;
    PropVariantClear(&name);
    return equal;
}

HRESULT selectEndpoint(IMMDeviceEnumerator *enumerator, const std::wstring &requested, ComPtr<IMMDevice> &device)
{
    if (requested.empty()) {
        IMMDevice *raw = nullptr;
        const HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &raw);
        device.reset(raw);
        return hr;
    }
    IMMDeviceCollection *raw = nullptr;
    HRESULT hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &raw);
    ComPtr<IMMDeviceCollection> candidates(raw);
    if (FAILED(hr)) return hr;
    UINT count = 0;
    hr = candidates->GetCount(&count);
    if (FAILED(hr)) return hr;
    ComPtr<IMMDevice> named;
    for (UINT i = 0; i < count; ++i) {
        IMMDevice *entry = nullptr;
        hr = candidates->Item(i, &entry);
        ComPtr<IMMDevice> candidate(entry);
        if (FAILED(hr) || !candidate) continue;
        if (_wcsicmp(endpointId(candidate.get()).c_str(), requested.c_str()) == 0) {
            device = std::move(candidate);
            return S_OK;
        }
        if (!named && friendlyNameMatches(candidate.get(), requested))
            named = std::move(candidate);
    }
    device = std::move(named);
    return device ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

SpectrumWindows::PcmFormat pcmFormat(const WAVEFORMATEX &wave)
{
    auto tag = wave.wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (wave.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) return {};
        const auto &extended = reinterpret_cast<const WAVEFORMATEXTENSIBLE &>(wave);
        if (IsEqualGUID(extended.SubFormat, kPcm)) tag = WAVE_FORMAT_PCM;
        else if (IsEqualGUID(extended.SubFormat, kFloat)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else return {};
    }
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT) return {};
    return {wave.nSamplesPerSec, wave.nChannels, wave.wBitsPerSample, wave.nBlockAlign,
        tag == WAVE_FORMAT_IEEE_FLOAT};
}

bool writePcm(HANDLE output, const std::vector<std::int16_t> &samples)
{
    const auto *data = reinterpret_cast<const unsigned char *>(samples.data());
    auto left = static_cast<DWORD>(samples.size() * sizeof(samples.front()));
    while (left) {
        DWORD written = 0;
        if (!WriteFile(output, data, left, &written, nullptr) || !written) return false;
        data += written;
        left -= written;
    }
    return true;
}

HRESULT loopback(IMMDeviceEnumerator *enumerator, const std::wstring &requested, HANDLE parent, HANDLE output)
{
    ComPtr<IMMDevice> endpoint;
    HRESULT hr = selectEndpoint(enumerator, requested, endpoint);
    if (FAILED(hr)) return hr;
    const auto id = endpointId(endpoint.get());
    IAudioClient *rawClient = nullptr;
    hr = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&rawClient));
    ComPtr<IAudioClient> client(rawClient);
    if (FAILED(hr)) return hr;
    WAVEFORMATEX *rawFormat = nullptr;
    hr = client->GetMixFormat(&rawFormat);
    const std::unique_ptr<WAVEFORMATEX, TaskFree> mix(rawFormat);
    if (FAILED(hr)) return hr;
    const auto format = pcmFormat(*mix);
    if (!format.valid()) return AUDCLNT_E_UNSUPPORTED_FORMAT;
    const auto flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 0, 0, mix.get(), nullptr);
    if (FAILED(hr)) return hr;
    const UniqueHandle event(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!event) return HRESULT_FROM_WIN32(GetLastError());
    if (FAILED(hr = client->SetEventHandle(event.get()))) return hr;
    IAudioCaptureClient *rawCapture = nullptr;
    hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(&rawCapture));
    ComPtr<IAudioCaptureClient> capture(rawCapture);
    if (FAILED(hr)) return hr;
    if (FAILED(hr = client->Start())) return hr;
    struct StopClient {
        IAudioClient *client;
        ~StopClient() { client->Stop(); }
    } stop{client.get()};

    SpectrumWindows::Converter convert(format);
    std::vector<std::int16_t> samples;
    samples.reserve(4096);
    const HANDLE handles[] = {parent, event.get()};
    auto lastPacket = GetTickCount64();
    auto lastSilence = lastPacket;
    auto lastDeviceCheck = lastPacket;
    while (true) {
        const auto wake = WaitForMultipleObjects(2, handles, FALSE, 20);
        if (wake == WAIT_OBJECT_0) return S_FALSE;
        if (wake == WAIT_FAILED) return HRESULT_FROM_WIN32(GetLastError());
        const auto now = GetTickCount64();
        if (requested.empty() && now - lastDeviceCheck >= 1000) {
            lastDeviceCheck = now;
            ComPtr<IMMDevice> current;
            if (FAILED(hr = selectEndpoint(enumerator, requested, current))) return hr;
            if (id != endpointId(current.get())) return S_OK;
        }
        // Drain bounded packets on the same thread, releasing every buffer before writing PCM.
        for (int i = 0; i < 32; ++i) {
            UINT32 available = 0;
            if (FAILED(hr = capture->GetNextPacketSize(&available))) return hr;
            if (!available) break;
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD packetFlags = 0;
            hr = capture->GetBuffer(&data, &frames, &packetFlags, nullptr, nullptr);
            if (FAILED(hr)) return hr;
            if (hr == AUDCLNT_S_BUFFER_EMPTY) break;
            samples.clear();
            if (packetFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) convert.reset();
            if (frames <= format.rate)
                convert.append(packetFlags & AUDCLNT_BUFFERFLAGS_SILENT ? nullptr : data, frames, samples);
            if (FAILED(hr = capture->ReleaseBuffer(frames))) return hr;
            if (!writePcm(output, samples)) return S_FALSE;
            lastPacket = now;
        }
        if (now - lastPacket >= 100 && now - lastSilence >= 20) {
            lastSilence = now;
            convert.reset();
            samples.assign(SpectrumWindows::kOutputRate / 50, 0);
            if (!writePcm(output, samples)) return S_FALSE;
        }
    }
}

int capturePlayback(HANDLE parent, HANDLE output, const std::wstring &device)
{
    IMMDeviceEnumerator *raw = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IMMDeviceEnumerator), reinterpret_cast<void **>(&raw));
    ComPtr<IMMDeviceEnumerator> enumerator(raw);
    if (FAILED(hr)) return 1;
    HRESULT lastError = S_OK;
    while (WaitForSingleObject(parent, 0) == WAIT_TIMEOUT) {
        hr = loopback(enumerator.get(), device, parent, output);
        if (hr == S_FALSE) return 0;
        if (FAILED(hr) && hr != lastError) {
            std::fprintf(stderr, "Windows playback unavailable (0x%08lx)\n", static_cast<unsigned long>(hr));
            std::fflush(stderr);
        }
        lastError = hr;
        if (FAILED(hr) && WaitForSingleObject(parent, 1500) != WAIT_TIMEOUT) return 0;
    }
    return 0;
}
}

int wmain(int argc, wchar_t **argv)
{
    if (argc != 2 && argc != 3) return 2;
    wchar_t *end = nullptr;
    errno = 0;
    const auto id = std::wcstoul(argv[1], &end, 10);
    if (errno || end == argv[1] || *end || !id || id > std::numeric_limits<DWORD>::max()) return 2;
    const UniqueHandle parent(OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(id)));
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!parent || !output || output == INVALID_HANDLE_VALUE) return 2;
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return 1;
    const int result = capturePlayback(parent.get(), output, argc == 3 ? std::wstring(argv[2]) : std::wstring());
    CoUninitialize();
    return result;
}
