#include "KrellmoonPlugin.h"

#include "theme/Theme.h"
#include "widgets/Panel.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

namespace {

// Reference new moon: 2000-01-06 18:14:00 UTC. Standard astronomical anchor;
// fmod against the synodic month gives a phase fraction accurate to a few
// minutes over decades, which is well within the resolution this widget
// renders at (8 named phases + 28 image frames at most).
QDateTime referenceNewMoon()
{
    return QDateTime::fromString(QStringLiteral("2000-01-06T18:14:00Z"),
                                 Qt::ISODate).toUTC();
}

constexpr double kSynodicDays = 29.530588853;

// Phase fraction in [0, 1): 0 = new, 0.25 = first quarter, 0.5 = full,
// 0.75 = last quarter.
double computeMoonPhase(const QDateTime &nowUtc)
{
    const QDateTime ref = referenceNewMoon();
    const qint64 secs = ref.secsTo(nowUtc);
    const double days = static_cast<double>(secs) / 86400.0;
    double k = std::fmod(days / kSynodicDays, 1.0);
    if (k < 0.0) k += 1.0;
    return k;
}

QString phaseName(double k)
{
    // 8 standard phase names. Quarter-points get a narrow band (~11 hours
    // either side, 1/64 of the cycle) so a phase that has just crossed
    // first quarter still reads as "FIRST QUARTER" briefly before flipping
    // to the next gibbous/crescent label.
    constexpr double q = 1.0 / 64.0;
    if (k < q || k > 1.0 - q)        return QStringLiteral("NEW MOON");
    if (std::abs(k - 0.25) < q)      return QStringLiteral("FIRST QUARTER");
    if (std::abs(k - 0.5)  < q)      return QStringLiteral("FULL MOON");
    if (std::abs(k - 0.75) < q)      return QStringLiteral("LAST QUARTER");
    if (k < 0.25)                    return QStringLiteral("WAXING CRESCENT");
    if (k < 0.5)                     return QStringLiteral("WAXING GIBBOUS");
    if (k < 0.75)                    return QStringLiteral("WANING GIBBOUS");
    return QStringLiteral("WANING CRESCENT");
}

// 28-frame day-in-cycle index (0=new, 14=full). Used for picking from a
// fine-grained image strip. Wraps to 0..27 so callers can index directly.
int dayInCycle(double k)
{
    int day = static_cast<int>(k * 28.0);
    if (day < 0) day = 0;
    if (day >= 28) day = day % 28;
    return day;
}

// Slug-form name for the 8 coarse phase image candidates.
QString phaseSlug(double k)
{
    constexpr double q = 1.0 / 64.0;
    if (k < q || k > 1.0 - q)        return QStringLiteral("new");
    if (std::abs(k - 0.25) < q)      return QStringLiteral("first_quarter");
    if (std::abs(k - 0.5)  < q)      return QStringLiteral("full");
    if (std::abs(k - 0.75) < q)      return QStringLiteral("last_quarter");
    if (k < 0.25)                    return QStringLiteral("waxing_crescent");
    if (k < 0.5)                     return QStringLiteral("waxing_gibbous");
    if (k < 0.75)                    return QStringLiteral("waning_gibbous");
    return QStringLiteral("waning_crescent");
}

QStringList moonAssetSearchDirs()
{
    QStringList dirs;
    const QString userData =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!userData.isEmpty())
        dirs << userData + QStringLiteral("/plugins/krellmoon");
    dirs << QStringLiteral("/usr/share/krellix/plugins/krellmoon");
    dirs << QStringLiteral("/usr/local/share/krellix/plugins/krellmoon");
    return dirs;
}

// Resolve the on-disk PNG for the current phase, with a hardened lookup:
//   1. Per-day fine frame: day_NN.png (NN = 00..27)
//   2. Coarse phase slug: phase_<slug>.png
//   3. Empty pixmap (caller draws procedural fallback)
//
// Each candidate is canonicalized and verified to live inside one of the
// allowed asset roots — settings-driven path injection (e.g. a hostile
// theme dropping a config file) cannot walk out of the search dirs.
QPixmap loadMoonPixmap(double k)
{
    const int day = dayInCycle(k);
    const QString slug = phaseSlug(k);

    const QStringList dirs = moonAssetSearchDirs();
    const QStringList names = {
        QStringLiteral("day_%1.png").arg(day, 2, 10, QLatin1Char('0')),
        QStringLiteral("phase_%1.png").arg(slug),
    };

    for (const QString &dir : dirs) {
        const QString rootCanon = QFileInfo(dir).canonicalFilePath();
        if (rootCanon.isEmpty()) continue;
        for (const QString &name : names) {
            const QString candidate = rootCanon + QLatin1Char('/') + name;
            const QFileInfo info(candidate);
            if (!info.exists() || !info.isFile()) continue;
            const QString fileCanon = info.canonicalFilePath();
            if (fileCanon.isEmpty()) continue;
            if (!fileCanon.startsWith(rootCanon + QLatin1Char('/')))
                continue;
            QPixmap pix(fileCanon);
            if (!pix.isNull()) return pix;
        }
    }
    return {};
}

// Procedural moon — used when no PNG asset is found. Two-ellipse method:
// full disk in dark, lit semicircle on the lit side, then a terminator
// ellipse painted in lit (gibbous) or dark (crescent) to fix the boundary.
// Looks reasonable at 48–96 px; not photorealistic, but always available.
void drawProceduralMoon(QPainter &p, const QRectF &r, double k,
                        const QColor &lit, const QColor &dark)
{
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    const double cx = r.center().x();
    const double cy = r.center().y();
    const double R = qMin(r.width(), r.height()) / 2.0 - 1.0;
    if (R <= 0.5) return;

    const QRectF disk(cx - R, cy - R, 2.0 * R, 2.0 * R);

    p.setBrush(dark);
    p.drawEllipse(disk);

    if (k < 0.005 || k > 0.995) return;  // new moon: fully dark

    const bool waxing  = (k < 0.5);
    const bool gibbous = (k > 0.25 && k < 0.75);

    QPainterPath halfDisk;
    halfDisk.moveTo(cx, cy - R);
    halfDisk.arcTo(disk, 90.0, waxing ? -180.0 : 180.0);
    halfDisk.closeSubpath();
    p.setBrush(lit);
    p.drawPath(halfDisk);

    if (std::abs(k - 0.5) < 0.005) {
        p.drawEllipse(disk);  // full moon — paint the other half lit too
        return;
    }

    const double termHalfW = R * std::abs(std::cos(2.0 * M_PI * k));
    if (termHalfW < 0.5) return;  // pure quarter — no terminator visible

    const QRectF termEllipse(cx - termHalfW, cy - R,
                             2.0 * termHalfW, 2.0 * R);
    p.setBrush(gibbous ? lit : dark);
    p.drawEllipse(termEllipse);
}

class MoonWidget : public QWidget
{
public:
    explicit MoonWidget(Theme *theme, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_theme(theme)
    {
        const int size = qBound(24, QSettings().value(
            QStringLiteral("plugins/krellmoon/size"), 64).toInt(), 256);
        setFixedSize(size, size);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_pixmap = loadMoonPixmap(m_phase);
    }

    void setPhase(double k)
    {
        if (qFuzzyCompare(k, m_phase)) return;
        m_phase = k;
        m_pixmap = loadMoonPixmap(m_phase);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (!m_pixmap.isNull()) {
            // Scale to widget bounds preserving aspect; SmoothTransformation
            // for the typical case of source larger than display size.
            const QPixmap scaled = m_pixmap.scaled(
                size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const int x = (width()  - scaled.width())  / 2;
            const int y = (height() - scaled.height()) / 2;
            p.drawPixmap(x, y, scaled);
            return;
        }

        const QColor lit = m_theme
            ? m_theme->color(QStringLiteral("text_primary"),
                             QColor(232, 235, 240))
            : QColor(232, 235, 240);
        const QColor dark = m_theme
            ? m_theme->color(QStringLiteral("chart_bg"),
                             QColor(20, 24, 32))
            : QColor(20, 24, 32);
        drawProceduralMoon(p, QRectF(rect()), m_phase, lit, dark);
    }

private:
    Theme *m_theme = nullptr;
    QPixmap m_pixmap;
    double m_phase = 0.0;
};

QString cssColor(const QColor &color)
{
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

} // namespace

KrellmoonMonitor::KrellmoonMonitor(Theme *theme, QObject *parent)
    : MonitorBase(theme, parent)
{
}

KrellmoonMonitor::~KrellmoonMonitor()
{
    shutdown();
}

QString KrellmoonMonitor::id() const
{
    return QStringLiteral("krellmoon");
}

QString KrellmoonMonitor::displayName() const
{
    return QStringLiteral("Krellmoon");
}

int KrellmoonMonitor::tickIntervalMs() const
{
    // Phase advances ~12° per day. One hour is plenty for a refresh; longer
    // is fine too. Settings can override (units: ms).
    return QSettings().value(QStringLiteral("plugins/krellmoon/interval_ms"),
                             3600000).toInt();
}

QWidget *KrellmoonMonitor::createWidget(QWidget *parent)
{
    auto *panel = new Panel(theme(), parent);
    panel->setSurfaceKey(QStringLiteral("panel_bg_krellmoon"));

    auto *body = new QWidget(panel);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(2);

    auto *moon = new MoonWidget(theme(), body);
    m_moon = moon;
    auto *moonRow = new QHBoxLayout;
    moonRow->setContentsMargins(0, 0, 0, 0);
    moonRow->addStretch(1);
    moonRow->addWidget(moon);
    moonRow->addStretch(1);

    m_phaseLabel = new QLabel(QStringLiteral("--"), body);
    m_phaseLabel->setAlignment(Qt::AlignCenter);

    layout->addLayout(moonRow);
    layout->addWidget(m_phaseLabel);
    panel->addWidget(body);

    applyThemeColors();
    connect(theme(), &Theme::themeChanged, this, [this]() {
        applyThemeColors();
        if (m_moon) m_moon->update();
    });

    refresh();
    return panel;
}

void KrellmoonMonitor::tick()
{
    refresh();
}

void KrellmoonMonitor::shutdown()
{
    m_tearingDown = true;
    m_moon = nullptr;
    m_phaseLabel = nullptr;
}

void KrellmoonMonitor::applyThemeColors()
{
    if (!m_phaseLabel) return;
    const QColor accent = theme()->textStyle(
        QStringLiteral("text_accent"),
        QStringLiteral("text_primary")).color;
    m_phaseLabel->setStyleSheet(QStringLiteral(
        "font-size: 9px; font-weight: 700; letter-spacing: 1px; color: %1;")
        .arg(cssColor(accent)));
}

void KrellmoonMonitor::refresh()
{
    if (m_tearingDown) return;
    const double k = computeMoonPhase(QDateTime::currentDateTimeUtc());
    // m_moon is only ever assigned a MoonWidget in createWidget(); shutdown
    // nulls it before destruction, so the cast is safe whenever the pointer
    // is non-null. MoonWidget has no Q_OBJECT (it doesn't need signals),
    // so qobject_cast isn't an option here.
    if (QWidget *w = m_moon.data())
        static_cast<MoonWidget *>(w)->setPhase(k);
    if (m_phaseLabel) m_phaseLabel->setText(phaseName(k));
}

QString KrellmoonPlugin::pluginId() const
{
    return QStringLiteral("krellmoon");
}

QString KrellmoonPlugin::pluginName() const
{
    return QStringLiteral("Krellmoon");
}

QString KrellmoonPlugin::pluginVersion() const
{
    return QStringLiteral("0.1.0");
}

QList<MonitorBase *> KrellmoonPlugin::createMonitors(Theme *theme, QObject *parent)
{
    // Default OFF — plugins must be explicitly enabled in Settings before
    // they appear. Same convention as the rest of the plugin set.
    if (!QSettings().value(QStringLiteral("plugins/krellmoon/enabled"), false).toBool())
        return {};
    return {new KrellmoonMonitor(theme, parent)};
}
