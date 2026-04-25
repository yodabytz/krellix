#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class MonitorBase;
class Theme;
class QPluginLoader;

// Discovers, validates, and loads krellix plugins (Qt shared libraries
// implementing IKrellixPlugin). Hardened against path-traversal tricks:
// only files whose canonical path lives directly inside one of the search
// roots are considered. Bounded discovery (kMaxPlugins, kMaxLibBytes) so a
// pathological plugins directory cannot trigger unbounded I/O or memory.
class PluginLoader : public QObject
{
    Q_OBJECT

public:
    explicit PluginLoader(QObject *parent = nullptr);
    ~PluginLoader() override;

    // User-first list of directories scanned for plugin shared libraries.
    static QStringList searchPaths();

    // Walk the search paths, load each viable plugin, and accumulate the
    // monitors it produces. Loaders are retained for the lifetime of this
    // PluginLoader instance — never unloaded during runtime.
    QList<MonitorBase *> discoverAndLoad(Theme *theme,
                                         QObject *monitorParent);

private:
    QList<QPluginLoader *> m_loaders;

    Q_DISABLE_COPY_MOVE(PluginLoader)
};
