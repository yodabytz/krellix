#include "remote/RemoteSource.h"
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost)) return 1;
    RemoteSource remote;
    if (!remote.sensorReadings().isEmpty() || remote.batteryInfo().present || remote.remoteTime().isValid()) return 2;
    QTcpSocket *peer = nullptr;
    int stage = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&]() {
        peer = server.nextPendingConnection();
        peer->write(R"({"type":"sample","hostname":"test-server","kernel":"linux test","uptime":123,"datetime":"2026-10-18T12:00:00+02:00","cpu":[{"name":"cpu","i":-1,"user":12}],"mem":{"total":456},"net":[{"name":"servernet","rx":1234}],"disk":[{"name":"serverdisk","sr":5}],"proc":{"processes":42},"sensors":[{"chip":"remotechip","label":"remote cpu","type":0,"value":52.5,"max":80,"crit":95}],"battery":{"present":true,"percent":73,"status":"Charging","remaining":90},"packages":{"status":"ok","system":"apt","count":17},"health":{"critical":0,"warnings":0},"stack":{"containers":3,"running":2}})");
        peer->write("\n");
    });
    QObject::connect(&remote, &RemoteSource::sampleReceived, &app, [&]() {
        if (stage++ == 0) {
            if (remote.packageInfo().value(QStringLiteral("count")).toInt()!=17 || remote.sensorReadings().size()!=1 || remote.sensorReadings()[0].value!=52.5 || remote.sensorReadings()[0].critC!=95 || remote.batteryInfo().percent!=73 || remote.remoteTime().offsetFromUtc()!=7200 || remote.cpuSamples().size()!=1 || remote.memInfo().totalKb!=456 || remote.netSamples()[0].name!=QStringLiteral("servernet") || remote.diskSamples()[0].name!=QStringLiteral("serverdisk") || remote.procInfo().processes!=42 || remote.uptimeSeconds()!=123) qFatal("Remote telemetry mismatch");
            // An older server omits optional readings: never retain stale/local data.
            peer->write("{\"type\":\"sample\",\"uptime\":124}\n");
        } else {
            if (!remote.packageInfo().isEmpty() || !remote.sensorReadings().isEmpty() || remote.batteryInfo().present || remote.remoteTime().isValid()) qFatal("Missing readings must clear cached data");
            peer->disconnectFromHost();
        }
    });
    QObject::connect(&remote, &RemoteSource::connectionStateChanged, &app, [&](bool connected) {
        if (!connected && stage==2) {
            if (!remote.packageInfo().isEmpty() || !remote.cpuSamples().isEmpty() || remote.uptimeSeconds()!=-1) qFatal("Disconnect did not clear data");
            std::puts("PASS: remote samples, optional fields, time zone, and disconnect");
            app.exit(0);
        }
    });
    QTimer::singleShot(5000,&app,[&](){app.exit(3);});
    remote.connectToHost(QStringLiteral("127.0.0.1"),server.serverPort());
    return app.exec();
}
