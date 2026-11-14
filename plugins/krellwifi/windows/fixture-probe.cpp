#include <QCoreApplication>
#include <QFile>
#include <QThread>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cstdio>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);QFile out;out.open(stdout,QIODevice::WriteOnly);
    const auto mode=qgetenv("KRELLWIFI_TEST_CASE");
    if(mode=="timeout"){QThread::sleep(60);return 0;}
    if(mode=="oversize"){out.write(QByteArray(1024*1024+100,'x'));return 0;}
    if(mode=="malformed"){out.write("not-json\n");return 0;}
    QJsonObject object{{"state",QString::fromLatin1(mode)},{"detail","fixture"}};
    if(mode=="ok")object.insert("aps",QJsonArray{QJsonObject{{"ssid","<b>TEST</b>"},{"bssid","001122334455"},{"signal",91},{"khz",5180000},{"protected",true}}});
    out.write(QJsonDocument(object).toJson(QJsonDocument::Compact));return 0;
}
