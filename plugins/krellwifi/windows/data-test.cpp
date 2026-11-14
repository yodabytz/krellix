#include "NativeWifi.h"
#include "WifiData.h"
#include <QCoreApplication>
#include <cstring>
#include <vector>
static void check(bool v,const char *m){if(!v)qFatal("%s",m);}
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    check(channelLabel(2412000)==QStringLiteral("2.4GHz ch1"),"2.4GHz channel wrong");
    check(channelLabel(5180000)==QStringLiteral("5GHz ch36"),"5GHz channel wrong");
    check(channelLabel(5955000)==QStringLiteral("6GHz ch1"),"6GHz channel wrong");
    check(channelLabel(5935000)==QStringLiteral("6GHz ch2"),"6GHz exception wrong");
    QJsonArray rows;std::vector<BYTE> bytes(sizeof(WLAN_BSS_LIST)+sizeof(WLAN_BSS_ENTRY));
    auto *list=reinterpret_cast<WLAN_BSS_LIST *>(bytes.data());list->dwTotalSize=DWORD(bytes.size());list->dwNumberOfItems=2;
    for(int i=0;i<2;++i){auto &r=list->wlanBssEntries[i];r.dot11Ssid.uSSIDLength=4;std::memcpy(r.dot11Ssid.ucSSID,"TEST",4);r.dot11Bssid[5]=BYTE(i+1);r.uLinkQuality=80+i;r.ulChCenterFrequency=2412000;r.usCapabilityInformation=i?0x10:0;}
    check(decodeBss(list,rows)&&rows.size()==2,"Valid native records rejected");
    // IE offsets are never dereferenced, even if a hostile AP supplies nonsense.
    list->wlanBssEntries[0].ulIeOffset=0xffffffff;list->wlanBssEntries[0].ulIeSize=0xffffffff;
    QJsonArray ignored;check(decodeBss(list,ignored),"Parser touched untrusted IE blob");
    auto view=wifiView({{"state","ok"},{"detail","test"},{"aps",rows}});
    check(view.valid&&view.summary==QStringLiteral("2 APs  best 81%")&&view.tooltip.contains("1 open; 1 privacy-enabled"),"Summary/security wrong");
    rows.append(rows.first());view=wifiView({{"state","ok"},{"detail","test"},{"aps",rows}});
    check(view.summary.startsWith("2 APs"),"Duplicate BSSID counted twice");
    list->wlanBssEntries[0].dot11Ssid.uSSIDLength=33;check(!decodeBss(list,ignored),"Oversized SSID accepted");
    list->wlanBssEntries[0].dot11Ssid.uSSIDLength=4;list->dwTotalSize=4;check(!decodeBss(list,ignored),"Truncated buffer accepted");
    list->dwTotalSize=DWORD(bytes.size());list->dwNumberOfItems=4097;check(!decodeBss(list,ignored),"Oversized count accepted");
    check(!wifiView({}).valid,"Missing scan considered valid");
    check(wifiView({{"state","ok"},{"detail",""},{"aps",QJsonArray{}}}).summary==QStringLiteral("No access points"),"Empty scan confused with no adapter");
    check(wifiView({{"state","no_adapter"},{"detail","VM"}}).summary==QStringLiteral("No Wi-Fi adapter"),"Hardware absence wrong");
    check(cleanSsid(QStringLiteral("\n\u202e"))==QStringLiteral("(hidden)"),"Control characters unsanitized");
    qInfo("PASS native buffer bounds, SSIDs, ignored IE blobs, channels, AP deduplication, security summary and empty/absent states");
}
