#include "StackData.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <cstdio>
static void print(const QByteArray &bytes){fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);fputc('\n',stdout);}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();const auto mode=qgetenv("KRELLSTACK_TEST_CASE");
    if(args.contains("--child")){QThread::sleep(90);return 0;}
    if(mode=="timeout"){
        if(!qEnvironmentVariableIsEmpty("KRELLSTACK_CHILD_FILE")){
            qint64 pid=0;QProcess::startDetached(app.applicationFilePath(),{"--child"},{},&pid);
            QFile file(qEnvironmentVariable("KRELLSTACK_CHILD_FILE"));
            if(!file.open(QIODevice::WriteOnly))return 2;
            file.write(QByteArray::number(pid));
        }
        QThread::sleep(90);return 1;
    }
    if(mode=="oversize"){print(QByteArray(1100000,'x'));return 0;}
    if(mode=="malformed"){print("bad JSON");return 0;}
    const auto tool=QFileInfo(app.applicationFilePath()).baseName();
    if(tool=="docker"||tool=="podman"||tool=="kubectl"||tool=="ssh"){
        if(mode=="error"){fprintf(stderr,"Permission denied\n");return 1;}
        if(!qEnvironmentVariableIsEmpty("KRELLSTACK_ARGS_FILE")){
            QFile file(qEnvironmentVariable("KRELLSTACK_ARGS_FILE"));
            if(!file.open(QIODevice::Append))return 2;
            file.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact)+'\n');
        }
        if(tool=="podman"&&args.contains("connection")){
            print(mode=="remote"?"[{\"Name\":\"remote\",\"Default\":true,\"URI\":\"ssh://user@example.com/run/podman.sock\"}]":"[{\"Name\":\"machine\",\"Default\":true,\"URI\":\"ssh://user@127.0.0.1:1234/run/podman.sock\"}]");return 0;
        }
        if(tool=="kubectl"){
            if(args.contains("current-context"))print("desktop");
            else if(args.contains("view"))print(mode=="remote"?"https://example.com:6443":"https://127.0.0.1:6443");
            else print("{\"items\":[{\"metadata\":{\"uid\":\"pod1\"},\"status\":{\"phase\":\"Running\",\"conditions\":[{\"type\":\"Ready\",\"status\":\"True\"}]}}]}");
            return 0;
        }
        if(mode=="empty")return 0;
        if(args.contains("image"))print("{\"kind\":\"image\",\"id\":\"image1\"}\n{\"kind\":\"image\",\"id\":\"image1\"}");
        else if(args.contains("network"))print("{\"kind\":\"network\",\"id\":\"net1\"}");
        else if(args.contains("health=unhealthy"))print("{\"kind\":\"unhealthy\",\"id\":\"one\"}");
        else print("{\"kind\":\"container\",\"id\":\"one\",\"state\":\"running\",\"project\":\"project1\"}\n{\"kind\":\"container\",\"id\":\"two\",\"state\":\"exited\",\"project\":\"project1\"}");
        return 0;
    }
    WindowsStack::Source source{"<b>Docker</b>","ok",{}, {2,1,1,1,1,1,0}};
    if(mode=="error"){source.status="error";source.detail="Engine unavailable";}
    if(mode=="docker-error"){source.name="Docker";source.status="error";source.detail="Engine unavailable";}
    if(mode=="empty")source.counts={};
    if(mode=="missing"){source.status="missing";source.detail="No tools installed";}
    QJsonArray sources{WindowsStack::json(source)};
    if(mode=="partial")sources.append(WindowsStack::json({"Podman","error","Machine unavailable",{}}));
    print(QJsonDocument(QJsonObject{{"sources",sources}}).toJson(QJsonDocument::Compact));
}
