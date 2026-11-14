#include <QCoreApplication>
#include <QStandardPaths>

static void isolatePluginDiscovery()
{
    // Test executables are not the exported krellix.exe plugin host. Never load
    // the user's installed DLLs or access the user's application data in them.
    QStandardPaths::setTestModeEnabled(true);
}
Q_COREAPP_STARTUP_FUNCTION(isolatePluginDiscovery)
