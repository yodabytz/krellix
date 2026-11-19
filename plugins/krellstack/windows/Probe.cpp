#include "StackData.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>
#include <cstdio>
#include <windows.h>

namespace {
QString executable(const QString &name,const QStringList &extras={}) {
    QStringList paths=extras;
    for(const auto &path:qEnvironmentVariable("PATH").split(';',Qt::SkipEmptyParts))
        if(QDir::isAbsolutePath(path) && QDir(path).absolutePath()!=QDir::currentPath())paths.append(path);
    return QStandardPaths::findExecutable(name+".exe",paths);
}
struct Result { bool ok=false; QByteArray output; QString error; };
Result run(const QString &program,const QStringList &arguments,int timeout=5000) {
    Result result; QProcess process;
    auto environment=QProcessEnvironment::systemEnvironment();
    environment.remove("DOCKER_HOST"); environment.remove("DOCKER_CONTEXT");
    process.setProcessEnvironment(environment); process.setProgram(program); process.setArguments(arguments);
    process.start(); QElapsedTimer timer; timer.start(); qint64 bytes=0; QByteArray errors;
    while(true) {
        process.waitForReadyRead(50);
        for(bool error:{false,true}) {
            process.setReadChannel(error?QProcess::StandardError:QProcess::StandardOutput);
            const auto chunk=process.read(qMax<qint64>(1,1024*1024-bytes+1)); bytes+=chunk.size();
            if(error)errors+=chunk;else result.output+=chunk;
        }
        if(bytes>1024*1024 || timer.elapsed()>timeout) {
            process.kill(); process.waitForFinished(500);
            result.error=bytes>1024*1024?"Response too large":"Container query timed out"; return result;
        }
        if(process.state()==QProcess::NotRunning)break;
    }
    result.ok=process.exitStatus()==QProcess::NormalExit && process.exitCode()==0 && process.error()!=QProcess::FailedToStart;
    if(!result.ok) result.error=errors.trimmed().isEmpty()?"Container command failed":QString::fromUtf8(errors).left(512);
    return result;
}
QList<QStringList> commands(bool podman=false) {
    const QString container=podman
        ? QStringLiteral("{\"kind\":\"container\",\"id\":{{json .ID}},\"state\":{{json .State}}}")
        : QStringLiteral("{\"kind\":\"container\",\"id\":{{json .ID}},\"state\":{{json .State}},\"project\":{{json (.Label \"com.docker.compose.project\")}}}");
    return {{"ps","--all","--no-trunc","--format",container},
        {"image","ls","--no-trunc","--format","{\"kind\":\"image\",\"id\":{{json .ID}}}"},
        {"network","ls","--no-trunc","--format","{\"kind\":\"network\",\"id\":{{json .ID}}}"},
        {"ps","--no-trunc","--filter","health=unhealthy","--format","{\"kind\":\"unhealthy\",\"id\":{{json .ID}}}"}};
}
WindowsStack::Source runtime(const QString &name,const QString &program,const QStringList &prefix,bool podman=false) {
    WindowsStack::Source source{name,"error",{}, {}}; QByteArray output;
    for(const auto &arguments:commands(podman)) {
        const auto result=run(program,prefix+arguments);
        if(!result.ok){source.detail=result.error;return source;}
        output+=result.output+'\n';
        if(output.size()>1024*1024){source.detail="Container response too large";return source;}
    }
    if(WindowsStack::records(output,source.counts,source.detail))source.status="ok";
    if(podman && source.status=="ok")source.detail="Local Podman machine; Compose labels are not counted.";
    return source;
}
WindowsStack::Source ssh(const QString &host,const QString &program) {
    WindowsStack::Source source{"SSH "+host,"error",{}, {}};
    if(program.isEmpty()){source.detail="Windows OpenSSH client not found";return source;}
    QStringList script;
    for(const auto &arguments:commands()) {
        QStringList quoted;
        for(const auto &argument:arguments) quoted.append("'"+argument+"'");
        script.append("docker "+quoted.join(' '));
    }
    const auto result=run(program,{"-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","ConnectTimeout=3",
        "-o","ConnectionAttempts=1","-o","ServerAliveInterval=2","-o","ServerAliveCountMax=1",host,script.join(" && ")},7000);
    if(!result.ok){source.detail=result.error;return source;}
    if(WindowsStack::records(result.output,source.counts,source.detail))source.status="ok";
    return source;
}
bool loopback(const QUrl &uri) {
    const auto host=uri.host().toLower();
    return host=="localhost" || host=="127.0.0.1" || host=="::1";
}
WindowsStack::Source podman(const QString &program) {
    WindowsStack::Source source{"Podman","error",{}, {}};
    const auto connections=run(program,{"system","connection","list","--format","json"});
    const auto document=QJsonDocument::fromJson(connections.output);
    if(!connections.ok || !document.isArray()){source.detail="Cannot read Podman machine connections";return source;}
    for(const auto &value:document.array()) {
        const auto row=value.toObject(); if(!row.value("Default").toBool())continue;
        const auto uri=QUrl(row.value("URI").toString()); const auto name=row.value("Name").toString();
        if(uri.scheme()!="ssh" || !loopback(uri) || name.isEmpty() || name.size()>256 || name.startsWith('-')) {
            source.detail="Default Podman connection is not a local loopback machine; not queried.";return source;
        }
        return runtime("Podman",program,{"--connection",name},true);
    }
    source.status="missing";source.detail="No default local Podman machine connection";return source;
}
WindowsStack::Source kubernetes(const QString &program) {
    WindowsStack::Source source{"Kubernetes","error",{}, {}};
    const auto context=run(program,{"config","current-context"});
    const auto name=QString::fromUtf8(context.output).trimmed();
    if(!context.ok){
        source.detail=context.error;
        if(context.error.contains("current-context is not set")){source.status="missing";source.detail="No Kubernetes context configured";}
        return source;
    }
    if(name.isEmpty()){source.status="missing";source.detail="No Kubernetes context configured";return source;}
    if(name.size()>256 || name.startsWith('-')){source.detail="Invalid Kubernetes context";return source;}
    const auto endpoint=run(program,{"config","view","--minify","--context",name,"-o","jsonpath={.clusters[0].cluster.server}"});
    const auto url=QUrl(QString::fromUtf8(endpoint.output).trimmed());
    if(!endpoint.ok || (url.scheme()!="https" && url.scheme()!="http") || (!loopback(url) && url.host()!="kubernetes.docker.internal")) {
        source.detail="Kubernetes context is not a local cluster; not queried.";return source;
    }
    const auto result=run(program,{"--context",name,"get","pods","--all-namespaces","-o","json","--request-timeout=4s"});
    if(!result.ok){source.detail=result.error;return source;}
    if(!WindowsStack::pods(result.output,source.counts)){source.detail="Invalid Kubernetes pod data";return source;}
    source.status="ok"; source.detail="Local context: "+name+"; all namespaces. Pods are counted separately from containers.";return source;
}
bool containChildren() {
    // Keep the handle until process exit: closing it kills this helper and all
    // descendants, including credential/SSH subprocesses, on timeout or shutdown.
    HANDLE job=CreateJobObjectW(nullptr,nullptr); if(!job)return false;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{}; info.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&info,sizeof(info)) || !AssignProcessToJobObject(job,GetCurrentProcess())) {
        CloseHandle(job); return false;
    }
    return true;
}
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QJsonArray sources;
    auto failure=[&](const QString &detail){sources.append(WindowsStack::json({"Stack","error",detail,{}}));};
    if(!containChildren())failure("Cannot establish safe subprocess cleanup");
    else {
        QFile input;
        if(!input.open(stdin,QIODevice::ReadOnly))return 1;
        const auto bytes=input.read(8193);
        const auto document=QJsonDocument::fromJson(bytes); QStringList hosts;
        if(bytes.size()>8192 || !document.isObject() || !document.object().value("hosts").isString() || !WindowsStack::hosts(document.object().value("hosts").toString(),hosts))
            failure("Invalid SSH hosts: use at most four hostnames or user@host entries.");
        else if(!hosts.isEmpty()) {
            const auto client=executable("ssh",{qEnvironmentVariable("WINDIR")+"/System32/OpenSSH"});
            for(const auto &host:hosts)sources.append(WindowsStack::json(ssh(host,client)));
        }else {
            const auto docker=executable("docker",{qEnvironmentVariable("ProgramFiles")+"/Docker/Docker/resources/bin"});
            const auto podmanPath=executable("podman",{qEnvironmentVariable("ProgramFiles")+"/RedHat/Podman"});
            const auto kubectl=executable("kubectl",{qEnvironmentVariable("ProgramFiles")+"/Docker/Docker/resources/bin"});
            if(!docker.isEmpty())sources.append(WindowsStack::json(runtime("Docker",docker,{"--host","npipe:////./pipe/docker_engine"})));
            if(!podmanPath.isEmpty())sources.append(WindowsStack::json(podman(podmanPath)));
            if(!kubectl.isEmpty())sources.append(WindowsStack::json(kubernetes(kubectl)));
            if(sources.isEmpty())sources.append(WindowsStack::json({"Local","missing","No Docker, Podman or kubectl executable found. No runtime is installed automatically.",{}}));
        }
    }
    QFile output;
    if(!output.open(stdout,QIODevice::WriteOnly))return 1;
    output.write(QJsonDocument(QJsonObject{{"sources",sources}}).toJson(QJsonDocument::Compact)); output.write("\n"); output.flush();
}
