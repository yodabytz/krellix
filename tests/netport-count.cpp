#include "monitors/NetPortMonitor.h"
#include "sysdep/NetPortStat.h"
#include "theme/Theme.h"
#include <QApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QWidget>
#include <QDebug>
#include <QEvent>

static QList<NetPortSample> samples;
static QList<NetPortSample> readSamples() { return samples; }

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir dir;
    QCoreApplication::setOrganizationName(QStringLiteral("netport-test"));
    QCoreApplication::setApplicationName(QStringLiteral("netport-test"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings settings;
    settings.setValue(QStringLiteral("monitors/netports/watch1/enabled"), true);
    settings.setValue(QStringLiteral("monitors/netports/watch1/label"), QStringLiteral("WWW"));
    settings.setValue(QStringLiteral("monitors/netports/watch1/ports"), QStringLiteral("80, 443"));
    settings.sync();
    NetPortStat::setReadOverride(readSamples);
    Theme theme;
    NetPortMonitor monitor(&theme);
    QWidget *panel = monitor.createWidget(nullptr);
    for (const QString &protocol : {QStringLiteral("tcp"), QStringLiteral("udp"), QStringLiteral("all")}) {
      for (int primary : {80, 65000}) {
        const int secondary = primary == 80 ? 443 : 65001;
        settings.setValue(QStringLiteral("monitors/netports/watch1/protocol"), protocol);
        settings.setValue(QStringLiteral("monitors/netports/watch1/ports"),
            primary == 80 ? QStringLiteral("80, 443") : QStringLiteral("65000-65001"));
        settings.sync();
        for (int count : {0, 1, 12, 13, 57, 99, 100, 137, 4}) {
        samples.clear();
        for (int i = 0; i < count; ++i)
            samples.append({protocol == QStringLiteral("all")
                    ? (i % 2 ? QStringLiteral("tcp") : QStringLiteral("udp")) : protocol,
                quint16(i % 2 ? primary : secondary),
                quint16(10000 + i), QStringLiteral("192.0.2.1"), QStringLiteral("01")});
        // None of these are established connections to the watched local ports.
        samples.append({QStringLiteral("tcp"), 443, 0, {}, QStringLiteral("0A")});
        samples.append({QStringLiteral("tcp"), 443, 1234, QStringLiteral("192.0.2.2"), QStringLiteral("03")});
        samples.append({QStringLiteral("tcp"), 443, 1234, QStringLiteral("192.0.2.2"), QStringLiteral("06")});
        samples.append({QStringLiteral("tcp"), 5555, 443, QStringLiteral("192.0.2.2"), QStringLiteral("01")});
        monitor.tick();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        bool found = false;
        for (QWidget *widget : panel->findChildren<QWidget *>()) {
            if (!widget->toolTip().startsWith(QStringLiteral("WWW:"))) continue;
            found = true;
            if (!widget->toolTip().startsWith(QStringLiteral("WWW: %1 connection").arg(count)))
                qFatal("Wrong count: %s", qPrintable(widget->toolTip()));
            if (count > 12 && !widget->toolTip().contains(QStringLiteral("... %1 more").arg(count - 12)))
                qFatal("Wrong tooltip overflow");
        }
        if (!found) qFatal("Watch row missing");
        }
      }
    }
    settings.setValue(QStringLiteral("monitors/netports/watch1/ports"), QStringLiteral("22,60000-61000"));
    settings.setValue(QStringLiteral("monitors/netports/watch1/protocol"), QStringLiteral("all"));
    settings.sync();
    samples = {
        {QStringLiteral("tcp"), 22, 1234, QStringLiteral("192.0.2.1"), QStringLiteral("01")},
        {QStringLiteral("tcp"), 22, 1235, QStringLiteral("192.0.2.2"), QStringLiteral("ESTABLISHED")},
        {QStringLiteral("tcp"), 22, 0, {}, QStringLiteral("LISTEN")},
        {QStringLiteral("udp"), 60000, 0, {}, {}},
        {QStringLiteral("udp"), 61000, 0, {}, {}},
        {QStringLiteral("udp"), 60001, 50000, QStringLiteral("192.0.2.3"), {}},
        {QStringLiteral("udp"), 61001, 0, {}, {}},
        {QStringLiteral("udp"), 59999, 0, {}, {}}
    };
    monitor.tick();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    bool moshFound = false;
    for (QWidget *widget : panel->findChildren<QWidget *>()) {
        if (!widget->toolTip().startsWith(QStringLiteral("WWW:"))) continue;
        moshFound = widget->toolTip().startsWith(QStringLiteral("WWW: 5 connections"))
            && widget->toolTip().contains(QStringLiteral("UDP :60000"))
            && widget->toolTip().contains(QStringLiteral("UDP :61000"));
    }
    if (!moshFound) qFatal("SSH/Mosh watch count incorrect");
    delete panel;
    NetPortStat::setReadOverride(nullptr);
    qInfo("PASS: full counts, tooltip limits, TCP states, ports, and live decreases");
}
