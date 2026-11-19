#pragma once
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <cmath>

namespace WindowsStack {
struct Counts { int containers=0, running=0, images=0, compose=0, networks=0, unhealthy=0, pods=0; };
struct Source { QString name, status, detail; Counts counts; };
struct Snapshot { bool valid=false; QList<Source> sources; };
inline bool count(const QJsonObject &object, const char *key, int &result) {
    if (!object.value(key).isDouble()) return false;
    const double value=object.value(key).toDouble(-1);
    if (!std::isfinite(value) || value<0 || value>1000000 || std::floor(value)!=value) return false;
    result=int(value); return true;
}
inline QJsonObject json(const Source &source) {
    const auto &c=source.counts;
    return {{"name",source.name},{"status",source.status},{"detail",source.detail},
        {"containers",c.containers},{"running",c.running},{"images",c.images},
        {"compose",c.compose},{"networks",c.networks},{"unhealthy",c.unhealthy},{"pods",c.pods}};
}
inline Snapshot parse(const QJsonObject &object) {
    Snapshot out;
    if (!object.value("sources").isArray()) return out;
    const auto rows=object.value("sources").toArray();
    if (rows.isEmpty() || rows.size()>8) return out;
    QSet<QString> names;
    for (const auto &value:rows) {
        if (!value.isObject()) return out;
        const auto row=value.toObject(); Source source;
        source.name=row.value("name").toString(); source.status=row.value("status").toString();
        source.detail=row.value("detail").toString(); auto &c=source.counts;
        if (source.name.isEmpty() || source.name.size()>256 || source.detail.size()>4096 || names.contains(source.name)) return out;
        names.insert(source.name);
        if (source.status!="ok" && source.status!="error" && source.status!="missing") return out;
        if (source.status=="ok" && (!count(row,"containers",c.containers) || !count(row,"running",c.running)
            || !count(row,"images",c.images) || !count(row,"compose",c.compose) || !count(row,"networks",c.networks)
            || !count(row,"unhealthy",c.unhealthy) || !count(row,"pods",c.pods)
            || c.running>c.containers || c.unhealthy>c.containers+c.pods)) return out;
        out.sources.append(source);
    }
    out.valid=true; return out;
}
inline bool hosts(const QString &raw, QStringList &out) {
    if (raw.size()>4096) return false;
    const QRegularExpression allowed(QStringLiteral("^(?:[A-Za-z0-9_][A-Za-z0-9_.-]*@)?[A-Za-z0-9_][A-Za-z0-9_.:-]*$"));
    for (const auto &part:raw.split(QRegularExpression("[,;\n]"),Qt::SkipEmptyParts)) {
        const auto host=part.trimmed(); if(host.isEmpty()) continue;
        if(host.size()>255 || !allowed.match(host).hasMatch()) return false;
        if(!out.contains(host,Qt::CaseInsensitive)) out.append(host);
    }
    return out.size()<=4;
}
inline bool records(const QByteArray &output, Counts &counts, QString &error) {
    if(output.size()>1024*1024){error="Container data too large";return false;}
    QSet<QString> containers, running, images, projects, networks, attention;
    const QSet<QString> states{"created","initialized","running","paused","restarting","removing","exited","dead","stopped","stopping","configured","unknown"};
    for(const auto &line:output.split('\n')) {
        if(line.trimmed().isEmpty())continue;
        const auto document=QJsonDocument::fromJson(line);
        if(!document.isObject()){error="Invalid container JSON";return false;}
        const auto row=document.object(); const auto kind=row.value("kind").toString(); const auto id=row.value("id").toString();
        if(id.isEmpty() || id.size()>256){error="Invalid container identifier";return false;}
        if(kind=="container") {
            const auto state=row.value("state").toString().toLower();
            if(!states.contains(state)){error="Unknown container state";return false;}
            containers.insert(id); if(state=="running")running.insert(id);
            if(state=="restarting" || state=="dead" || state=="exited" || state=="stopped" || state=="unknown")attention.insert(id);
            const auto project=row.value("project").toString();
            if(project.size()>256){error="Invalid Compose project";return false;}
            if(!project.isEmpty())projects.insert(project);
        }else if(kind=="image")images.insert(id);
        else if(kind=="network")networks.insert(id);
        else if(kind=="unhealthy")attention.insert(id);
        else {error="Unknown container record";return false;}
        if(containers.size()+images.size()+networks.size()>16384){error="Too many container records";return false;}
    }
    // Lists are separate read-only requests; ignore containers removed between them.
    attention.intersect(containers);
    counts={int(containers.size()),int(running.size()),int(images.size()),int(projects.size()),int(networks.size()),int(attention.size()),0};
    return true;
}
inline bool pods(const QByteArray &output, Counts &counts) {
    const auto document=QJsonDocument::fromJson(output);
    if(!document.isObject() || !document.object().value("items").isArray())return false;
    const auto items=document.object().value("items").toArray(); if(items.size()>16384)return false;
    QSet<QString> seen;
    for(const auto &value:items) {
        const auto pod=value.toObject(); const auto uid=pod.value("metadata").toObject().value("uid").toString();
        if(uid.isEmpty() || uid.size()>256)return false;
        if(seen.contains(uid))continue;
        seen.insert(uid);
        const auto status=pod.value("status").toObject(); const auto phase=status.value("phase").toString();
        if(phase!="Pending" && phase!="Running" && phase!="Succeeded" && phase!="Failed" && phase!="Unknown")return false;
        ++counts.pods;
        bool ready=false;
        for(const auto &condition:status.value("conditions").toArray()) {
            const auto object=condition.toObject();
            if(object.value("type")=="Ready" && object.value("status")=="True")ready=true;
        }
        if(phase!="Succeeded" && !(phase=="Running" && ready))++counts.unhealthy;
    }
    return true;
}
}
