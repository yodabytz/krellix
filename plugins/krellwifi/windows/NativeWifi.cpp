#include "NativeWifi.h"
#include "WifiData.h"
#include <iphlpapi.h>
#include <cstddef>
#include <vector>
#include <memory>

namespace {
struct WlanMemory { void *p=nullptr; ~WlanMemory(){if(p)WlanFreeMemory(p);} };
struct Client { HANDLE h=nullptr; ~Client(){if(h)WlanCloseHandle(h,nullptr);} };
struct Notice { GUID guid{}; HANDLE event=nullptr; volatile LONG result=0; };
void WINAPI notification(PWLAN_NOTIFICATION_DATA data,PVOID context)
{
    auto *notice=static_cast<Notice *>(context);
    if(!data || data->NotificationSource!=WLAN_NOTIFICATION_SOURCE_ACM || !IsEqualGUID(data->InterfaceGuid,notice->guid))return;
    if(data->NotificationCode==wlan_notification_acm_scan_complete){InterlockedExchange(&notice->result,1);SetEvent(notice->event);}
    else if(data->NotificationCode==wlan_notification_acm_scan_fail){InterlockedExchange(&notice->result,2);SetEvent(notice->event);}
}
QJsonObject state(const QString &kind,const QString &detail) {return {{"state",kind},{"detail",detail}};}
QJsonObject error(DWORD code)
{
    if(code==ERROR_ACCESS_DENIED)return state(QStringLiteral("permission"),QStringLiteral("Allow location access for desktop apps in Windows Settings if you want Wi-Fi scans. No permissions were changed."));
    if(code==ERROR_SERVICE_NOT_ACTIVE || code==ERROR_NOT_SUPPORTED)return state(QStringLiteral("service_off"),QStringLiteral("WLAN AutoConfig is unavailable or stopped. No service settings were changed."));
    if(code==0x80342002UL)return state(QStringLiteral("radio_off"),QStringLiteral("Enable the Wi-Fi radio in Windows to scan."));
    return state(QStringLiteral("error"),QStringLiteral("Native Wi-Fi error %1").arg(code));
}
bool noWirelessInterface()
{
    ULONG size=16384;std::vector<BYTE> buffer(size);
    ULONG result=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_ALL_INTERFACES,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()),&size);
    if(result==ERROR_BUFFER_OVERFLOW && size<=1024*1024){buffer.resize(size);result=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_ALL_INTERFACES,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data()),&size);}
    if(result!=NO_ERROR)return false;
    for(auto *p=reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());p;p=p->Next)if(p->IfType==IF_TYPE_IEEE80211)return false;
    return true;
}
}
bool decodeBss(const WLAN_BSS_LIST *list,QJsonArray &rows)
{
    constexpr size_t offset=offsetof(WLAN_BSS_LIST,wlanBssEntries);
    if(!list || list->dwTotalSize<offset || list->dwTotalSize>4*1024*1024 || list->dwNumberOfItems>4096
        || list->dwNumberOfItems>(list->dwTotalSize-offset)/sizeof(WLAN_BSS_ENTRY))return false;
    for(DWORD i=0;i<list->dwNumberOfItems;++i){
        const auto &entry=list->wlanBssEntries[i];
        if(entry.dot11Ssid.uSSIDLength>32 || entry.uLinkQuality>100 || entry.ulChCenterFrequency<2000000 || entry.ulChCenterFrequency>7200000)return false;
        const QByteArray ssid(reinterpret_cast<const char *>(entry.dot11Ssid.ucSSID),int(entry.dot11Ssid.uSSIDLength));
        const QByteArray mac(reinterpret_cast<const char *>(entry.dot11Bssid),6);
        rows.append(QJsonObject{{"ssid",QString::fromUtf8(ssid)},{"bssid",QString::fromLatin1(mac.toHex())},
            {"signal",int(entry.uLinkQuality)},{"khz",int(entry.ulChCenterFrequency)},{"protected",bool(entry.usCapabilityInformation&0x10)}});
        if(rows.size()>4096)return false;
    }
    return true; // Do not dereference or parse untrusted information-element offsets/blobs.
}
QJsonObject scanWifi()
{
    Client client;DWORD version=0;
    DWORD result=WlanOpenHandle(2,nullptr,&version,&client.h);
    if(result!=ERROR_SUCCESS){
        if(noWirelessInterface())return state(QStringLiteral("no_adapter"),QStringLiteral("No Wi-Fi interface is exposed to Windows. VMware Ethernet cannot scan the Mac's Wi-Fi radio."));
        return error(result);
    }
    PWLAN_INTERFACE_INFO_LIST interfaces=nullptr;
    result=WlanEnumInterfaces(client.h,nullptr,&interfaces);WlanMemory interfaceMemory{interfaces};
    if(result!=ERROR_SUCCESS)return error(result);
    if(!interfaces || interfaces->dwNumberOfItems>32)return state(QStringLiteral("error"),QStringLiteral("Invalid Wi-Fi interface list"));
    if(!interfaces->dwNumberOfItems)return state(QStringLiteral("no_adapter"),QStringLiteral("Windows exposes no Wi-Fi adapter. A passed-through USB Wi-Fi adapter may be needed in a VM."));
    if(interfaces->dwNumberOfItems>4)return state(QStringLiteral("error"),QStringLiteral("More than four Wi-Fi adapters; scan limit reached."));
    QJsonArray rows;
    for(DWORD i=0;i<interfaces->dwNumberOfItems;++i){
        auto notice=std::make_unique<Notice>();notice->guid=interfaces->InterfaceInfo[i].InterfaceGuid;
        notice->event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!notice->event)return error(GetLastError());
        result=WlanRegisterNotification(client.h,WLAN_NOTIFICATION_SOURCE_ACM,FALSE,notification,notice.get(),nullptr,nullptr);
        if(result!=ERROR_SUCCESS){CloseHandle(notice->event);return error(result);}
        result=WlanScan(client.h,&notice->guid,nullptr,nullptr,nullptr);
        const DWORD waited=result==ERROR_SUCCESS?WaitForSingleObject(notice->event,4000):WAIT_FAILED;
        // Unregister waits for callbacks before their context and event are released.
        const DWORD unregistered=WlanRegisterNotification(client.h,WLAN_NOTIFICATION_SOURCE_NONE,FALSE,nullptr,nullptr,nullptr,nullptr);
        if(unregistered!=ERROR_SUCCESS){
            // Keep callback state/event alive until this one-shot helper exits if
            // the service cannot confirm cancellation. Never leave a dangling callback.
            notice.release();return error(unregistered);
        }
        CloseHandle(notice->event);
        if(result!=ERROR_SUCCESS)return error(result);
        if(waited!=WAIT_OBJECT_0)return state(QStringLiteral("timeout"),QStringLiteral("The Wi-Fi driver did not finish its scan in four seconds; stale results were cleared."));
        if(InterlockedCompareExchange(&notice->result,0,0)!=1)return state(QStringLiteral("error"),QStringLiteral("The Wi-Fi driver reported a failed scan."));
        PWLAN_BSS_LIST list=nullptr;
        result=WlanGetNetworkBssList(client.h,&notice->guid,nullptr,dot11_BSS_type_infrastructure,FALSE,nullptr,&list);
        WlanMemory listMemory{list};
        if(result!=ERROR_SUCCESS)return error(result);
        if(!decodeBss(list,rows))return state(QStringLiteral("error"),QStringLiteral("Invalid or oversized access-point data"));
    }
    return {{"state","ok"},{"detail","Completed native Windows scan. Local Wi-Fi only; no credentials read or connections changed."},{"aps",rows}};
}
