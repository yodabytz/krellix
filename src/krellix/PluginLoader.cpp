#include "PluginLoader.h"

#include "monitors/MonitorBase.h"
#include "sdk/KrellixPlugin.h"

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPluginLoader>
#include <QStandardPaths>

#ifndef KRELLIX_PLUGIN_SYSTEM_DIR
#  define KRELLIX_PLUGIN_SYSTEM_DIR "/usr/lib/krellix/plugins"
#endif

Q_LOGGING_CATEGORY(lcPlugin, "krellix.plugin")

namespace {

constexpr int    kMaxPlugins  = 64;
constexpr qint64 kMaxLibBytes = 32LL * 1024LL * 1024LL;  // 32 MiB

} // namespace

PluginLoader::PluginLoader(QObject *parent)
    : QObject(parent)
{
}

PluginLoader::~PluginLoader() = default;

QStringList PluginLoader::searchPaths()
{
    QStringList paths;

    const QString dataHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!dataHome.isEmpty())
        paths << QDir(dataHome).filePath(QStringLiteral("krellix/plugins"));

    paths << QString::fromUtf8(KRELLIX_PLUGIN_SYSTEM_DIR);

    // Convenience for development from a build tree.
    paths << QDir::currentPath() + QStringLiteral("/plugins");

    return paths;
}

QList<MonitorBase *> PluginLoader::discoverAndLoad(Theme *theme,
                                                   QObject *monitorParent)
{
    Q_ASSERT(theme);
    Q_ASSERT(monitorParent);

    QList<MonitorBase *> all;
    int loaded = 0;

    for (const QString &dirPath : searchPaths()) {
        QDir d(dirPath);
        if (!d.exists()) continue;

        const QString canonRoot = QFileInfo(dirPath).canonicalFilePath();
        if (canonRoot.isEmpty()) continue;

        const QStringList nameFilters{
            QStringLiteral("*.so"),
            QStringLiteral("*.dylib"),
            QStringLiteral("*.dll"),
        };
        const QFileInfoList entries =
            d.entryInfoList(nameFilters, QDir::Files | QDir::Readable);

        for (const QFileInfo &fi : entries) {
            if (loaded >= kMaxPlugins) {
                qCWarning(lcPlugin) << "plugin limit reached, skipping rest in"
                                    << dirPath;
                return all;
            }

            const QString candidate = fi.absoluteFilePath();
            const QString canon     = fi.canonicalFilePath();
            if (canon.isEmpty()
                || !canon.startsWith(canonRoot + QLatin1Char('/'))) {
                qCWarning(lcPlugin) << "rejecting plugin outside root"
                                    << candidate;
                continue;
            }

            if (fi.size() <= 0 || fi.size() > kMaxLibBytes) {
                qCWarning(lcPlugin) << "rejecting plugin with implausible size"
                                    << canon << fi.size();
                continue;
            }

            auto *loader = new QPluginLoader(canon, this);
            QObject *instance = loader->instance();
            if (!instance) {
                qCWarning(lcPlugin) << "failed to load" << canon
                                    << ":" << loader->errorString();
                loader->deleteLater();
                continue;
            }

            auto *plugin = qobject_cast<IKrellixPlugin *>(instance);
            if (!plugin) {
                qCWarning(lcPlugin) << "not a krellix plugin (bad IID?)" << canon;
                loader->unload();
                loader->deleteLater();
                continue;
            }

            qCInfo(lcPlugin).nospace()
                << "loaded plugin " << plugin->pluginId()
                << " " << plugin->pluginVersion()
                << " (factory ready)";

            m_loaders.append(loader);
            m_plugins.append(plugin);
            ++loaded;
        }
    }

    return all;
}

QList<MonitorBase *> PluginLoader::createMonitorsForAll(Theme *theme,
                                                        QObject *monitorParent)
{
    Q_ASSERT(theme);
    Q_ASSERT(monitorParent);

    QList<MonitorBase *> all;
    for (IKrellixPlugin *p : m_plugins) {
        if (!p) continue;
        const QList<MonitorBase *> monitors = p->createMonitors(theme, monitorParent);
        for (MonitorBase *m : monitors) {
            if (m) all.append(m);
        }
    }
    return all;
}
