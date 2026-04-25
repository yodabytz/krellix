#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>

#ifndef KRELLIX_THEMES_SYSTEM_DIR
#  define KRELLIX_THEMES_SYSTEM_DIR "/usr/share/krellix/themes"
#endif

Q_LOGGING_CATEGORY(lcTheme, "krellix.theme")

namespace {

constexpr int kMaxJsonBytes = 256 * 1024;  // hard cap on theme.json size

QColor colorFromString(const QString &s, const QColor &fallback)
{
    const QColor c(s);
    return c.isValid() ? c : fallback;
}

QFont fontFromJson(const QJsonObject &o, const QFont &fallback)
{
    QFont f = fallback;
    const QJsonValue family = o.value(QStringLiteral("family"));
    if (family.isString())
        f.setFamily(family.toString());

    const QJsonValue size = o.value(QStringLiteral("size"));
    if (size.isDouble()) {
        const int pt = static_cast<int>(size.toDouble());
        f.setPointSize(qBound(4, pt, 72));
    }

    const QJsonValue bold = o.value(QStringLiteral("bold"));
    if (bold.isBool()) f.setBold(bold.toBool());

    const QJsonValue italic = o.value(QStringLiteral("italic"));
    if (italic.isBool()) f.setItalic(italic.toBool());

    return f;
}

} // namespace

Theme::Theme(QObject *parent)
    : QObject(parent)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &Theme::onFileChanged);
    loadDefaults();
    m_name = QStringLiteral("default");
}

Theme::~Theme() = default;

QStringList Theme::searchPaths()
{
    QStringList paths;

    const QString configHome =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (!configHome.isEmpty())
        paths << QDir(configHome).filePath(QStringLiteral("krellix/themes"));

    paths << QString::fromUtf8(KRELLIX_THEMES_SYSTEM_DIR);

    // Convenience: allow running from the build/source tree without installing.
    paths << QDir::currentPath() + QStringLiteral("/themes");

    return paths;
}

bool Theme::isSafeThemeName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    if (!re.match(name).hasMatch())   return false;
    if (name.startsWith(QLatin1Char('.'))) return false;
    return true;
}

QString Theme::canonicalize(const QString &path)
{
    return QFileInfo(path).canonicalFilePath();
}

bool Theme::load(const QString &name)
{
    if (!isSafeThemeName(name)) {
        qCWarning(lcTheme) << "refusing unsafe theme name" << name;
        return false;
    }

    if (!m_jsonPath.isEmpty()) {
        m_watcher->removePath(m_jsonPath);
        m_jsonPath.clear();
    }

    for (const QString &root : searchPaths()) {
        const QString candidateDir  = QDir(root).filePath(name);
        const QString candidateJson =
            QDir(candidateDir).filePath(QStringLiteral("theme.json"));
        if (!QFileInfo::exists(candidateJson)) continue;

        const QString canonRoot = canonicalize(root);
        const QString canonJson = canonicalize(candidateJson);
        if (canonRoot.isEmpty() || canonJson.isEmpty()
            || !canonJson.startsWith(canonRoot + QLatin1Char('/'))) {
            qCWarning(lcTheme) << "refusing path outside search root" << candidateJson;
            continue;
        }

        loadDefaults();
        if (!parseJsonFile(canonJson)) {
            qCWarning(lcTheme) << "failed to parse" << canonJson;
            continue;
        }

        m_name     = name;
        m_rootDir  = canonicalize(candidateDir);
        m_jsonPath = canonJson;
        m_watcher->addPath(canonJson);
        emit themeChanged();
        return true;
    }

    qCWarning(lcTheme) << "theme" << name << "not found; using built-in defaults";
    loadDefaults();
    m_name = QStringLiteral("default");
    m_rootDir.clear();
    emit themeChanged();
    return false;
}

void Theme::onFileChanged(const QString &path)
{
    if (path != m_jsonPath) return;

    // Editors that save by atomic-rename break the inotify watch. Re-add it.
    loadDefaults();
    parseJsonFile(path);
    if (QFileInfo::exists(path) && !m_watcher->files().contains(path))
        m_watcher->addPath(path);
    emit themeChanged();
}

void Theme::loadDefaults()
{
    m_colors.clear();
    m_colors.insert(QStringLiteral("panel_bg"),        QColor( 10,  14,  10));
    m_colors.insert(QStringLiteral("panel_border"),    QColor( 26,  42,  26));
    m_colors.insert(QStringLiteral("panel_highlight"), QColor( 40,  72,  40));
    m_colors.insert(QStringLiteral("chart_bg"),        QColor(  0,   0,   0));
    m_colors.insert(QStringLiteral("chart_grid"),      QColor( 10,  58,  10));
    m_colors.insert(QStringLiteral("text_primary"),    QColor(127, 255, 127));
    m_colors.insert(QStringLiteral("text_secondary"),  QColor( 63, 191,  63));
    m_colors.insert(QStringLiteral("krell_track"),     QColor( 26,  58,  26));
    m_colors.insert(QStringLiteral("krell_indicator"), QColor(127, 255, 127));

    QFont label(QStringLiteral("Monospace"), 9);
    label.setStyleHint(QFont::TypeWriter);
    QFont value = label;
    value.setBold(true);

    m_fonts.clear();
    m_fonts.insert(QStringLiteral("label"), label);
    m_fonts.insert(QStringLiteral("value"), value);

    m_metrics.clear();
    m_metrics.insert(QStringLiteral("panel_padding"),    4);
    m_metrics.insert(QStringLiteral("panel_spacing"),    2);
    m_metrics.insert(QStringLiteral("panel_border"),     1);
    m_metrics.insert(QStringLiteral("panel_min_width"),  200);
    m_metrics.insert(QStringLiteral("krell_height"),     8);
    m_metrics.insert(QStringLiteral("chart_height"),     32);
    m_metrics.insert(QStringLiteral("chart_grid_lines"), 4);
}

bool Theme::parseJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    if (f.size() > kMaxJsonBytes) {
        qCWarning(lcTheme) << "theme.json too large, refusing" << path;
        return false;
    }

    const QByteArray bytes = f.readAll();
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcTheme) << "JSON parse error" << err.errorString() << "in" << path;
        return false;
    }
    const QJsonObject root = doc.object();

    const QJsonValue colorsVal = root.value(QStringLiteral("colors"));
    if (colorsVal.isObject()) {
        const QJsonObject c = colorsVal.toObject();
        for (auto it = c.constBegin(); it != c.constEnd(); ++it) {
            if (!it.value().isString()) continue;
            m_colors.insert(it.key(),
                            colorFromString(it.value().toString(),
                                            m_colors.value(it.key())));
        }
    }

    const QJsonValue fontsVal = root.value(QStringLiteral("fonts"));
    if (fontsVal.isObject()) {
        const QJsonObject fObj = fontsVal.toObject();
        for (auto it = fObj.constBegin(); it != fObj.constEnd(); ++it) {
            if (!it.value().isObject()) continue;
            m_fonts.insert(it.key(),
                           fontFromJson(it.value().toObject(),
                                        m_fonts.value(it.key())));
        }
    }

    const QJsonValue metricsVal = root.value(QStringLiteral("metrics"));
    if (metricsVal.isObject()) {
        const QJsonObject m = metricsVal.toObject();
        for (auto it = m.constBegin(); it != m.constEnd(); ++it) {
            if (!it.value().isDouble()) continue;
            const int v = static_cast<int>(it.value().toDouble());
            m_metrics.insert(it.key(), qBound(0, v, 4096));
        }
    }
    return true;
}

QColor Theme::color(const QString &key, const QColor &fallback) const
{
    return m_colors.value(key, fallback);
}

QFont Theme::font(const QString &key, const QFont &fallback) const
{
    return m_fonts.value(key, fallback);
}

int Theme::metric(const QString &key, int fallback) const
{
    return m_metrics.value(key, fallback);
}

QString Theme::assetPath(const QString &relativePath) const
{
    if (m_rootDir.isEmpty() || relativePath.isEmpty()) return {};
    if (relativePath.startsWith(QLatin1Char('/'))) return {};
    if (relativePath.contains(QStringLiteral(".."))) return {};

    const QString candidate = QDir(m_rootDir).filePath(relativePath);
    const QString canon     = canonicalize(candidate);
    if (canon.isEmpty()) return {};
    if (!canon.startsWith(m_rootDir + QLatin1Char('/'))) return {};
    return canon;
}
