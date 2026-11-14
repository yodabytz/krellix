#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <algorithm>

struct WifiView { QString summary, detail, tooltip; bool valid = false; };
inline QString channelLabel(int khz)
{
    if (khz == 2484000) return QStringLiteral("2.4GHz ch14");
    if (khz >= 2412000 && khz <= 2472000 && (khz-2407000)%5000 == 0)
        return QStringLiteral("2.4GHz ch%1").arg((khz-2407000)/5000);
    if (khz >= 5000000 && khz <= 5900000 && khz%5000 == 0)
        return QStringLiteral("5GHz ch%1").arg((khz-5000000)/5000);
    if (khz == 5935000) return QStringLiteral("6GHz ch2");
    if (khz >= 5955000 && khz <= 7115000 && (khz-5950000)%5000 == 0)
        return QStringLiteral("6GHz ch%1").arg((khz-5950000)/5000);
    return QStringLiteral("%1 MHz").arg(double(khz)/1000,0,'f',0);
}
inline QString cleanSsid(QString name)
{
    for (auto &c : name) if (c.isSpace() || c.category() == QChar::Other_Control || c.category() == QChar::Other_Format) c = QLatin1Char(' ');
    name = name.trimmed();
    return name.isEmpty() ? QStringLiteral("(hidden)") : name;
}
inline WifiView wifiView(const QJsonObject &object)
{
    WifiView out{QStringLiteral("WiFi unavailable"),QStringLiteral("invalid scan data"),{},false};
    if (!object.value("state").isString() || !object.value("detail").isString() || object.value("detail").toString().size()>2048) return out;
    const QString state = object.value("state").toString();
    const QMap<QString,QString> states{{"no_adapter","No Wi-Fi adapter"},{"service_off","WLAN service stopped"},
        {"permission","Wi-Fi permission needed"},{"radio_off","Wi-Fi radio off"},{"timeout","Wi-Fi scan timed out"},{"error","WiFi unavailable"}};
    if (states.contains(state)) {
        out.summary=states.value(state);out.detail=object.value("detail").toString();out.tooltip=out.summary+QStringLiteral("\n")+out.detail;out.valid=true;return out;
    }
    if (state != QStringLiteral("ok") || !object.value("aps").isArray()) return out;
    const auto rows=object.value("aps").toArray();if(rows.size()>4096)return out;
    QMap<QString,QJsonObject> unique;
    for(const auto &value:rows) {
        if(!value.isObject())return out;
        const auto row=value.toObject();
        const QString id=row.value("bssid").toString();
        const auto signal=row.value("signal");const auto frequency=row.value("khz");
        if(id.size()!=12 || std::any_of(id.cbegin(),id.cend(),[](QChar c){return !QStringLiteral("0123456789abcdef").contains(c);})
            || !row.value("ssid").isString() || row.value("ssid").toString().toUtf8().size()>128
            || !signal.isDouble() || signal.toDouble()!=signal.toInt(-1) || signal.toInt(-1)<0 || signal.toInt()>100
            || !frequency.isDouble() || frequency.toDouble()!=frequency.toInt(-1) || frequency.toInt()<2000000 || frequency.toInt()>7200000
            || !row.value("protected").isBool())return out;
        const QString key=id+QLatin1Char('/')+QString::number(frequency.toInt());
        if(!unique.contains(key) || unique.value(key).value("signal").toInt()<signal.toInt())unique.insert(key,row);
    }
    QMap<QString,int> channels;int best=-1,open=0;QString name;QStringList descriptions;
    for(const auto &row:unique) {
        const auto ssid=cleanSsid(row.value("ssid").toString());
        const auto channel=channelLabel(row.value("khz").toInt());++channels[channel];
        const int signal=row.value("signal").toInt();
        if(signal>best){best=signal;name=ssid;}
        if(!row.value("protected").toBool())++open;
        if(descriptions.size()<32)descriptions<<QStringLiteral("%1: %2%, %3, %4").arg(ssid).arg(signal).arg(channel,row.value("protected").toBool()?QStringLiteral("privacy-enabled"):QStringLiteral("open"));
    }
    QString busy;int count=0;
    for(auto it=channels.cbegin();it!=channels.cend();++it)if(it.value()>count){busy=it.key();count=it.value();}
    out.summary=unique.isEmpty()?QStringLiteral("No access points"):QStringLiteral("%1 APs  best %2%").arg(unique.size()).arg(best);
    out.detail=unique.isEmpty()?QStringLiteral("scan completed"):QStringLiteral("%1  %2 x%3").arg(name.left(18),busy).arg(count);
    out.tooltip=QStringLiteral("KrellWiFi: %1\n%2 open; %3 privacy-enabled. Privacy does not imply strong encryption.\n%4\n%5")
        .arg(out.summary).arg(open).arg(unique.size()-open).arg(object.value("detail").toString(),descriptions.join(QLatin1Char('\n')));
    if(unique.size()>32)out.tooltip+=QStringLiteral("\nOnly the first 32 AP details are listed.");
    out.valid=true;return out;
}
