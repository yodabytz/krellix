#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class MonitorBase;
class Theme;
class IKrellixPlugin;
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

    // Re-create monitor instances from the already-loaded plugins. Used by
    // MainWindow's live rebuild path so settings changes can re-construct
    // the panel stack without re-scanning the filesystem.
    QList<MonitorBase *> createMonitorsForAll(Theme *theme,
                                              QObject *monitorParent);

    QList<IKrellixPlugin *> loadedPlugins() const { return m_plugins; }

private:
    QList<QPluginLoader *>  m_loaders;
    QList<IKrellixPlugin *> m_plugins;     // non-owning; loaders own QObject

    Q_DISABLE_COPY_MOVE(PluginLoader)
};
