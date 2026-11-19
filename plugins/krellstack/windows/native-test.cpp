#include "StackData.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <windows.h>
static void check(bool ok,const char *message){if(!ok)qFatal("%s",message);}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);check(argc==3,"Missing paths");QTemporaryDir directory;
    auto environment=QProcessEnvironment::systemEnvironment();
    // Keep Qt DLL discovery while ensuring only the isolated fixture tools are found.
    wchar_t modulePath[32768]{};
    check(GetModuleFileNameW(GetModuleHandleW(L"Qt6Core.dll"),modulePath,32768)>0,"Qt runtime path missing");
    const auto qtDirectory=QFileInfo(QString::fromWCharArray(modulePath)).absolutePath();
    environment.insert("PATH",directory.path()+";"+qtDirectory+";"+environment.value("WINDIR")+"/System32");
    environment.insert("ProgramFiles",directory.path());environment.insert("WINDIR",directory.path());
    // Windows derives ProgramFiles from the architecture-specific variables on launch.
    environment.insert("ProgramW6432",directory.path());
    environment.insert("ProgramFiles(x86)",directory.path());
    environment.insert("KRELLSTACK_ARGS_FILE",directory.filePath("arguments.jsonl"));
    environment.insert("KRELLSTACK_CHILD_FILE",directory.filePath("child.txt"));
    auto run=[&](const QString &mode,const QString &hosts=QString()){
        environment.insert("KRELLSTACK_TEST_CASE",mode);QProcess process;process.setProcessEnvironment(environment);
        process.start(QString::fromLocal8Bit(argv[1]),{});check(process.waitForStarted(5000),"Native helper start failed");
        process.write(QJsonDocument(QJsonObject{{"hosts",hosts}}).toJson(QJsonDocument::Compact));process.closeWriteChannel();
        check(process.waitForFinished(45000),"Native helper hung");check(process.exitCode()==0,"Native helper failed");
        const auto document=QJsonDocument::fromJson(process.readAllStandardOutput());
        const auto snapshot=WindowsStack::parse(document.object());check(snapshot.valid,"Invalid native output");
        qInfo().noquote()<<"Helper fixture"<<mode<<document.toJson(QJsonDocument::Compact).left(4096);
        return snapshot;
    };
    check(QFile::copy(QString::fromLocal8Bit(argv[2]),directory.filePath("docker.exe")),"Docker fixture copy");
    auto result=run("ok");
    check(result.sources.size()==1&&result.sources[0].counts.containers==2&&result.sources[0].counts.images==1&&result.sources[0].counts.unhealthy==2,"Docker collection wrong");
    QFile arguments(directory.filePath("arguments.jsonl"));check(arguments.open(QIODevice::ReadOnly),"No argument trace");
    check(arguments.readAll().contains("npipe:////./pipe/docker_engine"),"Docker local endpoint not forced");arguments.close();
    for(const auto *mode:{"error","malformed","oversize","timeout"})check(run(mode).sources[0].status=="error","Failure shown as healthy");
    QFile child(directory.filePath("child.txt"));check(child.open(QIODevice::ReadOnly),"No child PID");
    HANDLE handle=OpenProcess(SYNCHRONIZE,FALSE,DWORD(child.readAll().toULongLong()));
    if(handle){check(WaitForSingleObject(handle,2000)==WAIT_OBJECT_0,"Timed-out helper left descendant process");CloseHandle(handle);}
    check(run("empty").sources[0].counts.containers==0,"Empty engine wrong");
    check(QFile::copy(QString::fromLocal8Bit(argv[2]),directory.filePath("podman.exe")),"Podman fixture copy");
    check(QFile::copy(QString::fromLocal8Bit(argv[2]),directory.filePath("kubectl.exe")),"Kubernetes fixture copy");
    result=run("ok");check(result.sources.size()==3&&result.sources[1].status=="ok"&&result.sources[2].counts.pods==1,"Local runtimes failed");
    result=run("remote");check(result.sources[1].status=="error"&&result.sources[2].status=="error","Remote local-runtime context queried");
    check(QFile::copy(QString::fromLocal8Bit(argv[2]),directory.filePath("ssh.exe")),"SSH fixture copy");
    result=run("ok","user@server");check(result.sources.size()==1&&result.sources[0].name=="SSH user@server","Explicit SSH scan wrong");
    result=run("ok","-oProxyCommand=bad");check(result.sources[0].status=="error","Unsafe SSH input accepted");
    qInfo("PASS actual helper, Docker/Podman/Kubernetes fixtures, local endpoints, explicit SSH, bounded output/time and descendant cleanup");
}
