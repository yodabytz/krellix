#include "HealthData.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <windows.h>
#include <winsvc.h>
#include <oleauto.h>
#include <vector>

namespace {
struct ServiceHandle {
    SC_HANDLE value;
    explicit ServiceHandle(SC_HANDLE handle) : value(handle) {}
    ~ServiceHandle() { if (value) CloseServiceHandle(value); }
    ServiceHandle(const ServiceHandle &) = delete;
    ServiceHandle &operator=(const ServiceHandle &) = delete;
};
void unavailable(HealthData &data, const QString &title)
{
    finding(data, QStringLiteral("info"), title, QStringLiteral("Unavailable at current permissions; no elevation requested."));
}
void reboot(HealthData &data)
{
    // Read-only Windows Update SystemInfo property; this does not scan/install updates.
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) { unavailable(data, QStringLiteral("Reboot check unavailable")); return; }
    CLSID clsid{};
    IDispatch *dispatch = nullptr;
    HRESULT hr = CLSIDFromProgID(L"Microsoft.Update.SystemInfo", &clsid);
    if (SUCCEEDED(hr)) hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void **>(&dispatch));
    VARIANT result; VariantInit(&result);
    if (SUCCEEDED(hr)) {
        wchar_t property[] = L"RebootRequired";
        LPOLESTR name = property;
        DISPID id{};
        hr = dispatch->GetIDsOfNames(IID_NULL, &name, 1, LOCALE_SYSTEM_DEFAULT, &id);
        DISPPARAMS parameters{};
        if (SUCCEEDED(hr)) hr = dispatch->Invoke(id, IID_NULL, LOCALE_SYSTEM_DEFAULT, DISPATCH_PROPERTYGET,
                                                &parameters, &result, nullptr, nullptr);
    }
    if (FAILED(hr) || result.vt != VT_BOOL) unavailable(data, QStringLiteral("Reboot check unavailable"));
    else {
        data.coverage << QStringLiteral("Windows Update reboot requirement");
        if (result.boolVal != VARIANT_FALSE)
            finding(data, QStringLiteral("warning"), QStringLiteral("Reboot required"), QStringLiteral("Windows Update reports a restart is required."));
    }
    VariantClear(&result);
    if (dispatch) dispatch->Release();
    CoUninitialize();
}
void services(HealthData &data)
{
    ServiceHandle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE));
    if (!manager.value) { unavailable(data, QStringLiteral("Service check unavailable")); return; }
    // SCM caps each response at 256 KiB. Bound both allocation and enumeration passes.
    std::vector<BYTE> buffer(256 * 1024);
    DWORD resume = 0;
    bool partial = false;
    for (int pass = 0; pass < 8; ++pass) {
        DWORD needed = 0, count = 0;
        const bool done = EnumServicesStatusExW(manager.value, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
            buffer.data(), DWORD(buffer.size()), &needed, &count, &resume, nullptr);
        const DWORD error = done ? ERROR_SUCCESS : GetLastError();
        if (!done && error != ERROR_MORE_DATA) { unavailable(data, QStringLiteral("Service check unavailable")); return; }
        auto *entries = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
        if (count > buffer.size() / sizeof(*entries)) { unavailable(data, QStringLiteral("Service response invalid")); return; }
        for (DWORD i = 0; i < count; ++i) {
            const auto &entry = entries[i];
            if (entry.ServiceStatusProcess.dwCurrentState != SERVICE_STOPPED
                || entry.ServiceStatusProcess.dwWin32ExitCode == NO_ERROR) continue;
            ServiceHandle service(OpenServiceW(manager.value, entry.lpServiceName, SERVICE_QUERY_CONFIG));
            if (!service.value) { partial = true; continue; }
            DWORD bytes = 0;
            QueryServiceConfigW(service.value, nullptr, 0, &bytes);
            if (bytes < sizeof(QUERY_SERVICE_CONFIGW) || bytes > 8192) { partial = true; continue; }
            std::vector<BYTE> config(bytes);
            auto *value = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(config.data());
            if (!QueryServiceConfigW(service.value, value, bytes, &bytes)) { partial = true; continue; }
            if (value->dwStartType == SERVICE_AUTO_START) {
                finding(data, QStringLiteral("warning"), QStringLiteral("Automatic service failed"),
                    QStringLiteral("%1: stopped with error %2").arg(QString::fromWCharArray(entry.lpDisplayName).left(180))
                        .arg(entry.ServiceStatusProcess.dwWin32ExitCode));
                if (data.findings.size() >= 128) { unavailable(data, QStringLiteral("Service finding limit reached")); return; }
            }
        }
        if (done) {
            data.coverage << QStringLiteral("Stopped automatic services reporting an error (not every stopped service)");
            if (partial) unavailable(data, QStringLiteral("Some service checks unavailable"));
            return;
        }
    }
    unavailable(data, QStringLiteral("Service enumeration limit reached"));
}
}
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    HealthData data;
    const DWORD drives = GetLogicalDrives();
    int checked = 0;
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (DWORD(1) << i))) continue;
        wchar_t root[] = L"A:\\";
        root[0] = wchar_t(L'A' + i);
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        ULARGE_INTEGER available{}, total{}, free{};
        if (GetDiskFreeSpaceExW(root, &available, &total, &free)) {
            spaceFinding(data, QString::fromWCharArray(root), total.QuadPart, available.QuadPart);
            ++checked;
        } else unavailable(data, QStringLiteral("Disk check unavailable: %1").arg(QString::fromWCharArray(root)));
    }
    if (checked) data.coverage << QStringLiteral("%1 local fixed volume(s), available space for current user").arg(checked);
    else unavailable(data, QStringLiteral("No fixed-volume checks completed"));
    MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory) && memory.ullTotalPhys) {
        data.coverage << QStringLiteral("Physical memory pressure");
        if (memory.dwMemoryLoad >= 90)
            finding(data, memory.dwMemoryLoad >= 98 ? QStringLiteral("critical") : QStringLiteral("warning"),
                QStringLiteral("Memory pressure"), QStringLiteral("%1% physical memory in use").arg(memory.dwMemoryLoad));
    } else unavailable(data, QStringLiteral("Memory check unavailable"));
    reboot(data);
    services(data);
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    const auto bytes = QJsonDocument(healthJson(data)).toJson(QJsonDocument::Compact) + '\n';
    return output.write(bytes) == bytes.size() ? 0 : 1;
}
