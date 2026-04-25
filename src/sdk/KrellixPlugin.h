#pragma once

// Public SDK header for krellix plugin authors.
//
// A plugin is a Qt shared library (.so / .dylib / .dll) that exports a
// QObject subclass implementing IKrellixPlugin and tagged with
//   Q_OBJECT
//   Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
//   Q_INTERFACES(IKrellixPlugin)
//
// At startup krellix scans its plugin search paths, instantiates each
// plugin, and calls createMonitors() to obtain MonitorBase instances to
// add to its panel stack.
//
// Plugin lifetime: krellix never unloads plugins during runtime — the
// monitors they produce live for the application's lifetime.
//
// Security: plugins run as the user, in-process. Only install plugins
// from trusted sources.

#include <QList>
#include <QString>
#include <QtPlugin>

class MonitorBase;
class Theme;
class QObject;

class IKrellixPlugin
{
public:
    virtual ~IKrellixPlugin() = default;

    // Stable, slug-style identifier (e.g. "io.example.gpu-temp"). Logged
    // and used to deduplicate accidentally-installed copies later.
    virtual QString pluginId() const = 0;

    // Human-readable display name.
    virtual QString pluginName() const = 0;

    // SemVer string ("1.2.3" or "1.2.3-beta").
    virtual QString pluginVersion() const = 0;

    // Build the monitors this plugin contributes. Each MonitorBase must be
    // parented to `parent` (krellix passes its MainWindow). Theme is shared
    // across all monitors and lives at least as long as the application.
    // Return an empty list if no monitors apply on this system.
    virtual QList<MonitorBase *> createMonitors(Theme *theme,
                                                QObject *parent) = 0;
};

#define KrellixPlugin_iid "io.krellix.IKrellixPlugin/1.0"

Q_DECLARE_INTERFACE(IKrellixPlugin, KrellixPlugin_iid)
