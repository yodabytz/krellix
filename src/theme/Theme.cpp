#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLinearGradient>
#include <QLoggingCategory>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QtMath>

#include <algorithm>

#ifndef KRELLIX_THEMES_SYSTEM_DIR
#  define KRELLIX_THEMES_SYSTEM_DIR "/usr/share/krellix/themes"
#endif

Q_LOGGING_CATEGORY(lcTheme, "krellix.theme")

namespace {

constexpr int kMaxJsonBytes = 256 * 1024;  // hard cap on theme.json size
constexpr qint64 kMaxImageBytes = 10LL * 1024LL * 1024LL;
constexpr qint64 kMaxImagePixels = 4096LL * 4096LL;

QPixmap loadBoundedPixmap(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile() || info.size() <= 0 || info.size() > kMaxImageBytes)
        return {};

    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.isValid()
        && static_cast<qint64>(size.width()) * size.height() > kMaxImagePixels) {
        return {};
    }

    const QImage image = reader.read();
    if (image.isNull())
        return {};
    if (static_cast<qint64>(image.width()) * image.height() > kMaxImagePixels)
        return {};
    return QPixmap::fromImage(image);
}

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

QStringList Theme::availableThemes()
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &root : searchPaths()) {
        QDir d(root);
        if (!d.exists()) continue;
        const QFileInfoList subs =
            d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable);
        for (const QFileInfo &sub : subs) {
            const QString name = sub.fileName();
            if (!isSafeThemeName(name))             continue;
            if (seen.contains(name))                continue;
            const QString jsonPath =
                sub.absoluteFilePath() + QStringLiteral("/theme.json");
            if (!QFileInfo::exists(jsonPath))       continue;
            seen.insert(name);
            result.append(name);
        }
    }
    std::sort(result.begin(), result.end(),
              [](const QString &a, const QString &b) {
                  return a.compare(b, Qt::CaseInsensitive) < 0;
              });
    return result;
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
    m_imagePaths.clear();
    m_imageInts.clear();
    m_imageStrings.clear();
    m_pixmapCache.clear();
    m_surfaces.clear();
    m_textStyles.clear();
    m_gradients.clear();

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
    QFont time = label;
    time.setPointSize(10);   // a touch larger than label; not bold — clock display

    m_fonts.clear();
    m_fonts.insert(QStringLiteral("label"), label);
    m_fonts.insert(QStringLiteral("value"), value);
    m_fonts.insert(QStringLiteral("time"),  time);

    m_metrics.clear();
    m_metrics.insert(QStringLiteral("panel_padding"),    4);
    m_metrics.insert(QStringLiteral("panel_spacing"),    2);
    m_metrics.insert(QStringLiteral("panel_border"),     1);
    m_metrics.insert(QStringLiteral("panel_min_width"),  100);
    m_metrics.insert(QStringLiteral("krell_height"),     8);
    m_metrics.insert(QStringLiteral("meter_height"),     0);
    m_metrics.insert(QStringLiteral("chart_height"),     32);
    m_metrics.insert(QStringLiteral("chart_grid_lines"), 6);
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
            // Plain string → QColor.
            if (it.value().isString()) {
                m_colors.insert(it.key(),
                                colorFromString(it.value().toString(),
                                                m_colors.value(it.key())));
                continue;
            }
            // Object → may be a gradient. Schema:
            //   { "gradient": "linear", "angle": 90,
            //     "stops": [[0.0, "#aaa"], [1.0, "#bbb"]] }
            // Falls through to no-op if the object doesn't look right.
            if (!it.value().isObject()) continue;
            const QJsonObject obj = it.value().toObject();
            const QJsonValue stopsVal = obj.value(QStringLiteral("stops"));
            if (!stopsVal.isArray()) continue;

            Gradient g;
            const QJsonValue angleVal = obj.value(QStringLiteral("angle"));
            if (angleVal.isDouble())
                g.angle = static_cast<int>(angleVal.toDouble()) % 360;
            // (only Linear supported for now; "type"/"gradient" string
            // is parsed for forward-compat but ignored)
            const QJsonArray sa = stopsVal.toArray();
            for (const QJsonValue &sv : sa) {
                if (!sv.isArray()) continue;
                const QJsonArray pair = sv.toArray();
                if (pair.size() < 2) continue;
                const qreal off = qBound(0.0, pair[0].toDouble(), 1.0);
                const QColor col(pair[1].toString());
                if (!col.isValid()) continue;
                g.stops.append({off, col});
            }
            if (g.stops.size() >= 2) {
                m_gradients.insert(it.key(), g);
                // Mirror the first stop into m_colors so callers that
                // still use color() get a sensible flat fallback.
                if (!m_colors.contains(it.key()))
                    m_colors.insert(it.key(), g.stops.first().second);
            }
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

    // v2 surface dictionary (image + slice + opacity + tint per key).
    // Parsed before the legacy "images" block so a key explicitly
    // declared under "surfaces" wins over the v1 path-only entry.
    const QJsonValue surfacesVal = root.value(QStringLiteral("surfaces"));
    if (surfacesVal.isObject()) parseSurfaces(surfacesVal.toObject());

    // v2 text styles dictionary (color + drop shadow per key).
    const QJsonValue textStylesVal = root.value(QStringLiteral("text_styles"));
    if (textStylesVal.isObject()) parseTextStyles(textStylesVal.toObject());

    // Image assets. Each entry is either a bare string filename or an
    // object with at least { "image": "...", ... } and optional integer
    // sub-fields like "frames".
    const QJsonValue imagesVal = root.value(QStringLiteral("images"));
    if (imagesVal.isObject()) {
        const QJsonObject imgs = imagesVal.toObject();
        for (auto it = imgs.constBegin(); it != imgs.constEnd(); ++it) {
            const QString key = it.key();
            if (it.value().isString()) {
                m_imagePaths.insert(key, it.value().toString());
            } else if (it.value().isObject()) {
                const QJsonObject obj = it.value().toObject();
                const QJsonValue imgVal = obj.value(QStringLiteral("image"));
                if (imgVal.isString())
                    m_imagePaths.insert(key, imgVal.toString());
                for (auto io = obj.constBegin(); io != obj.constEnd(); ++io) {
                    if (io.key() == QStringLiteral("image")) continue;
                    const QString fullKey = key + QLatin1Char('.') + io.key();
                    if (io.value().isDouble()) {
                        const int v = static_cast<int>(io.value().toDouble());
                        m_imageInts.insert(fullKey, qBound(0, v, 4096));
                    } else if (io.value().isString()) {
                        m_imageStrings.insert(fullKey, io.value().toString());
                    } else if (io.value().isBool()) {
                        m_imageInts.insert(fullKey, io.value().toBool() ? 1 : 0);
                    }
                }
            }
        }
    }
    return true;
}

QColor Theme::color(const QString &key, const QColor &fallback) const
{
    return m_colors.value(key, fallback);
}

QBrush Theme::brush(const QString &key,
                    const QRectF &rect,
                    const QColor &fallback) const
{
    const auto gIt = m_gradients.constFind(key);
    if (gIt != m_gradients.constEnd() && rect.isValid()) {
        const Gradient &g = gIt.value();
        // Map angle → start/end points on the rect. 0° = horizontal
        // L→R, 90° = vertical T→B, 180° = horizontal R→L, 270° =
        // vertical B→T. Anything in between projects onto the rect's
        // diagonal at that angle. Cheap trig — calculated once per
        // paint since brushes rebuild per call.
        const double rad = qDegreesToRadians(static_cast<double>(g.angle));
        const double dx  = std::cos(rad);
        const double dy  = std::sin(rad);
        const double cx  = rect.center().x();
        const double cy  = rect.center().y();
        // Half-extent along the gradient direction so endpoints land
        // exactly on the rect edge, not past it.
        const double hx  = rect.width()  * 0.5;
        const double hy  = rect.height() * 0.5;
        const double t   = std::abs(dx * hx) + std::abs(dy * hy);
        QLinearGradient lg(QPointF(cx - dx * t, cy - dy * t),
                           QPointF(cx + dx * t, cy + dy * t));
        for (const auto &stop : g.stops)
            lg.setColorAt(stop.first, stop.second);
        return QBrush(lg);
    }
    // No gradient (or invalid rect) — solid color brush.
    const QColor c = m_colors.value(key, fallback);
    return c.isValid() ? QBrush(c) : QBrush();
}

QFont Theme::font(const QString &key, const QFont &fallback) const
{
    return m_fonts.value(key, fallback);
}

int Theme::metric(const QString &key, int fallback) const
{
    return m_metrics.value(key, fallback);
}

void Theme::setMetric(const QString &key, int value)
{
    const int clamped = qBound(0, value, 4096);
    if (m_metrics.value(key) == clamped) return;
    m_metrics.insert(key, clamped);
    emit themeChanged();
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

QPixmap Theme::pixmap(const QString &key) const
{
    const auto cached = m_pixmapCache.constFind(key);
    if (cached != m_pixmapCache.constEnd()) return cached.value();

    const QString rel = m_imagePaths.value(key);
    if (rel.isEmpty()) {
        m_pixmapCache.insert(key, QPixmap());  // negative cache
        return {};
    }
    const QString full = assetPath(rel);
    if (full.isEmpty()) {
        qCWarning(lcTheme) << "image" << key << "rel" << rel
                           << "rejected (escapes theme dir or missing)";
        m_pixmapCache.insert(key, QPixmap());
        return {};
    }
    QPixmap pm = loadBoundedPixmap(full);
    if (pm.isNull()) {
        qCWarning(lcTheme) << "failed to load image" << full;
        m_pixmapCache.insert(key, QPixmap());
        return {};
    }
    m_pixmapCache.insert(key, pm);
    return pm;
}

int Theme::imageInt(const QString &key, int fallback) const
{
    return m_imageInts.value(key, fallback);
}

QString Theme::imageStr(const QString &key, const QString &fallback) const
{
    return m_imageStrings.value(key, fallback);
}

QString Theme::imageMode(const QString &key, const QString &fallback) const
{
    const QString mode = m_imageStrings.value(key + QStringLiteral(".mode"));
    if (mode.isEmpty()) return fallback;
    if (mode == QStringLiteral("stretch") || mode == QStringLiteral("tile"))
        return mode;
    return fallback;
}

// ---- v2 parsers + lookup ---------------------------------------------

void Theme::parseSurfaces(const QJsonObject &obj)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        const QJsonObject s = it.value().toObject();
        SurfaceSpec spec;
        const QJsonValue imgVal = s.value(QStringLiteral("image"));
        if (imgVal.isString()) spec.relImage = imgVal.toString();
        const QJsonValue slVal = s.value(QStringLiteral("slice"));
        if (slVal.isDouble())
            spec.slice = qBound(0, static_cast<int>(slVal.toDouble()), 256);
        const QJsonValue opVal = s.value(QStringLiteral("opacity"));
        if (opVal.isDouble())
            spec.opacity = qBound(0.0, opVal.toDouble(), 1.0);
        const QJsonValue tnVal = s.value(QStringLiteral("tint"));
        if (tnVal.isString()) spec.tint = QColor(tnVal.toString());
        m_surfaces.insert(it.key(), spec);
    }
}

void Theme::parseTextStyles(const QJsonObject &obj)
{
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (!it.value().isObject()) continue;
        const QJsonObject s = it.value().toObject();
        TextStyle ts;
        const QJsonValue colVal = s.value(QStringLiteral("color"));
        if (colVal.isString()) ts.color = QColor(colVal.toString());
        const QJsonValue shVal = s.value(QStringLiteral("shadow"));
        if (shVal.isObject()) {
            const QJsonObject sh = shVal.toObject();
            const QJsonValue x = sh.value(QStringLiteral("x"));
            const QJsonValue y = sh.value(QStringLiteral("y"));
            const QJsonValue b = sh.value(QStringLiteral("blur"));
            const QJsonValue c = sh.value(QStringLiteral("color"));
            if (x.isDouble())
                ts.shadow.offsetX = qBound(-32, static_cast<int>(x.toDouble()), 32);
            if (y.isDouble())
                ts.shadow.offsetY = qBound(-32, static_cast<int>(y.toDouble()), 32);
            if (b.isDouble())
                ts.shadow.blur = qBound(0, static_cast<int>(b.toDouble()), 32);
            if (c.isString()) ts.shadow.color = QColor(c.toString());
            ts.shadow.present = ts.shadow.color.isValid()
                              && (ts.shadow.offsetX != 0
                                  || ts.shadow.offsetY != 0
                                  || ts.shadow.blur > 0);
        }
        m_textStyles.insert(it.key(), ts);
    }
}

Theme::TextStyle Theme::textStyle(const QString &key,
                                  const QString &fallbackKey) const
{
    const auto it = m_textStyles.constFind(key);
    if (it != m_textStyles.constEnd()) {
        TextStyle ts = it.value();
        if (!ts.color.isValid())
            ts.color = m_colors.value(key, QColor(Qt::white));
        return ts;
    }
    if (m_colors.contains(key)) {
        TextStyle ts;
        ts.color = m_colors.value(key);
        return ts;
    }
    // Primary key isn't configured anywhere. Try the fallback chain
    // before giving up to plain white. Lets widgets ask for a specific
    // key (e.g. "chart_overlay") and gracefully degrade to a generic
    // one ("text_primary") on themes that don't define the specific.
    if (!fallbackKey.isEmpty() && fallbackKey != key)
        return textStyle(fallbackKey);
    TextStyle ts;
    ts.color = QColor(Qt::white);
    return ts;
}

Theme::Surface Theme::surface(const QString &key,
                              const QString &fallbackKey) const
{
    // Lookup chain: requested key, then caller-supplied fallback, then
    // the universal "panel_bg" base. First match wins; missing image
    // falls through to the next entry in the chain.
    QStringList chain;
    chain << key;
    if (!fallbackKey.isEmpty() && fallbackKey != key) chain << fallbackKey;
    if (key != QStringLiteral("panel_bg")
        && fallbackKey != QStringLiteral("panel_bg"))
        chain << QStringLiteral("panel_bg");

    Surface out;
    bool foundSpec = false;
    for (const QString &k : chain) {
        // Prefer v2 surface entry.
        const auto sIt = m_surfaces.constFind(k);
        if (sIt != m_surfaces.constEnd()) {
            const SurfaceSpec &sp = sIt.value();
            out.slice   = sp.slice;
            out.opacity = sp.opacity;
            out.tint    = sp.tint;
            if (!sp.relImage.isEmpty()) {
                // Inline-resolve through the same hardened path-loader
                // that pixmap() uses so the v2 entry doesn't bypass
                // sandboxing.
                const QString full = assetPath(sp.relImage);
                if (!full.isEmpty())
                    out.image = loadBoundedPixmap(full);
                if (!out.image.isNull()) {
                    foundSpec = true;
                    break;
                }
            } else {
                foundSpec = true;
                break;        // explicit no-image surface (color fallback)
            }
        }
        // v1 fallback: the legacy "images" entry.
        const QString rel = m_imagePaths.value(k);
        if (!rel.isEmpty()) {
            const QString full = assetPath(rel);
            if (!full.isEmpty())
                out.image = loadBoundedPixmap(full);
            if (!out.image.isNull()) {
                foundSpec = true;
                break;
            }
        }
    }
    if (!foundSpec) return Surface{};   // truly nothing — caller uses color
    return out;
}
