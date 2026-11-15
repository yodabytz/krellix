#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); QFile out;
    if (!out.open(stdout, QIODevice::WriteOnly)) return 1;
    const QByteArray mode = qgetenv("KRELLBT_TEST_CASE");
    if (mode == "timeout") { QThread::sleep(60); return 0; }
    if (mode == "malformed") { out.write("not-json"); return 0; }
    if (mode == "oversize") { out.write(QByteArray(300 * 1024, 'x')); return 0; }
    QJsonArray devices;
    if (mode == "ok") {
        for (int i = 0; i < 3; ++i) devices.append(QJsonObject{{"id", QString::number(i)},
            {"name", i == 0 ? "<b>Long mouse name</b>" : "Keyboard"}, {"address", "00:11:22:33:44:55"},
            {"connected", true}, {"battery", i == 0 ? 0 : i == 1 ? -1 : 100}, {"detail", "fixture"}});
    }
    QJsonObject root{{"state", mode == "empty" ? "ok" : QString::fromLatin1(mode)}, {"detail", "fixture"}, {"devices", devices}};
    out.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
