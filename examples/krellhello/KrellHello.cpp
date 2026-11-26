#include "monitors/MonitorBase.h"
#include "remote/RemoteSource.h"
#include "sdk/KrellixPlugin.h"
#include "widgets/Decal.h"
#include "widgets/Krell.h"
#include "widgets/Panel.h"

#include <QPointer>

class HelloMonitor final : public MonitorBase
{
public:
    using MonitorBase::MonitorBase;

    QString id() const override { return QStringLiteral("krellhello"); }
    QString displayName() const override { return QStringLiteral("KrellHello"); }
    int tickIntervalMs() const override { return 1000; }

    QWidget *createWidget(QWidget *parent) override
    {
        auto *panel = new Panel(theme(), parent);
        panel->setSurfaceKey(QStringLiteral("panel_bg_krellhello"));
        panel->setTitle(displayName());
        m_readout = panel->addDecal(QStringLiteral("label"));
        m_bar = panel->addKrell();
        tick();
        return panel;
    }

    void tick() override
    {
        if (!m_readout || !m_bar) return;
        m_step = (m_step + 1) % 10;
        m_readout->setText(QStringLiteral("Demo step %1 / 9").arg(m_step));
        m_bar->setValue(m_step / 9.0);
    }

private:
    QPointer<Decal> m_readout;
    QPointer<Krell> m_bar;
    int m_step = 0;
};

class KrellHelloPlugin final : public QObject, public IKrellixPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID KrellixPlugin_iid)
    Q_INTERFACES(IKrellixPlugin)

public:
    QString pluginId() const override { return QStringLiteral("io.example.krellhello"); }
    QString pluginName() const override { return QStringLiteral("KrellHello"); }
    QString pluginVersion() const override { return QStringLiteral("0.1.0"); }

    QList<MonitorBase *> createMonitors(Theme *theme, QObject *parent) override
    {
        if (RemoteSource::instance()) return {};
        return {new HelloMonitor(theme, parent)};
    }
};

#include "KrellHello.moc"
