#include "krellixd/PackageProbe.h"
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    qputenv("PATH", temp.path().toUtf8() + ":/usr/bin:/bin");
    auto check = [&](QByteArray script, QString expected, int count) {
        QFile fixture(temp.path() + QStringLiteral("/apt"));
        if (!fixture.open(QIODevice::WriteOnly | QIODevice::Truncate)) qFatal("Cannot create fixture");
        fixture.write("#!/bin/sh\n" + script); fixture.close();
        fixture.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        PackageProbe probe(nullptr);
        QElapsedTimer clock; clock.start();
        auto info = probe.snapshot();
        if (clock.elapsed() > 100 || info.value(QStringLiteral("status")) != QStringLiteral("checking")) qFatal("Probe blocked the telemetry loop");
        do {
            QCoreApplication::processEvents(); QThread::msleep(10); info = probe.snapshot();
        } while (info.value(QStringLiteral("status")) == QStringLiteral("checking") && clock.elapsed() < 5000);
        if (info.value(QStringLiteral("status")) != expected) qFatal("Incorrect status");
        if (expected == QStringLiteral("ok") && info.value(QStringLiteral("count")).toInt(-1) != count) qFatal("Incorrect update count");
        if (probe.snapshot() != info) qFatal("Five-minute cache not reused");
    };
    check("sleep 1\nprintf 'Listing...\\nopenssl/stable 3.0 amd64 [upgradable from: 2.0]\\nqt/stable 6.4 amd64 [upgradable from: 6.3]\\n'\n", QStringLiteral("ok"), 2);
    check("printf 'Listing...\\n'\n", QStringLiteral("ok"), 0);
    check("exit 1\n", QStringLiteral("error"), -1);
    check("echo unexpected-output\n", QStringLiteral("error"), -1);
    qInfo("PASS: asynchronous package checks, cache reuse, counts, zero updates, command failures and malformed output");
}
