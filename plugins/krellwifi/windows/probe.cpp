#include "NativeWifi.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>
int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    const auto bytes=QJsonDocument(scanWifi()).toJson(QJsonDocument::Compact)+'\n';
    QFile output;if(!output.open(stdout,QIODevice::WriteOnly))return 1;
    return output.write(bytes)==bytes.size()?0:1;
}
