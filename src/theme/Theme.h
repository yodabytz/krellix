#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QStringList>

class QFileSystemWatcher;

// Holds the visual configuration loaded from a theme directory's theme.json.
// Widgets read colors/fonts/metrics from here and connect to themeChanged()
// to repaint when the user (or a theme author iterating live) edits the file.
//
// Path handling is hardened: theme names are validated to a strict charset,
// and the resolved JSON file must canonicalize to a path inside one of the
// known search roots. This prevents `--theme ../../etc` style escapes.
class Theme : public QObject
{
    Q_OBJECT

public:
    explicit Theme(QObject *parent = nullptr);
    ~Theme() override;

    // User-first list of directories searched for a theme by name.
    static QStringList searchPaths();

    // Names of themes discoverable on this system (deduped, sorted).
    // A theme is "discoverable" if its directory contains a theme.json
    // and its name passes isSafeThemeName().
    static QStringList availableThemes();

    // Public so callers (e.g. the theme picker) can validate user input
    // before passing it to load().
    static bool isSafeThemeName(const QString &name);

    QString name() const { return m_name; }
    QString rootDir() const { return m_rootDir; }

    // Load by bare theme name (no slashes). Returns true on success.
    // On failure the built-in defaults remain active.
    bool load(const QString &name);

    QColor color(const QString &key, const QColor &fallback = QColor()) const;
    QFont  font (const QString &key, const QFont  &fallback = QFont())  const;
    int    metric(const QString &key, int fallback = 0) const;

    // Runtime override of a theme metric (e.g. user-set krell_height from
    // SettingsDialog). Triggers themeChanged() so widgets refresh.
    void   setMetric(const QString &key, int value);

    // Image assets declared under "images" in theme.json. pixmap() returns
    // an empty QPixmap if the key isn't set, the file is missing, or the
    // path would escape the theme directory. Cache is cleared on reload.
    // Sub-keys (e.g. "krell.frames") are integers from the same images
    // section — used to describe sprite layout.
    QPixmap pixmap(const QString &key) const;
    int     imageInt(const QString &key, int fallback = 0) const;
    QString imageStr(const QString &key, const QString &fallback = QString()) const;

    // Image rendering mode for keys that paint a region: "tile" (default)
    // or "stretch". Tile repeats the image across the area; stretch scales
    // the whole image to fill it (no aspect-ratio preservation).
    QString imageMode(const QString &key, const QString &fallback = QStringLiteral("tile")) const;

    // Resolve a relative sprite path under the current theme root, refusing
    // any path that would escape the theme directory. Empty on rejection.
    QString assetPath(const QString &relativePath) const;

signals:
    void themeChanged();

private slots:
    void onFileChanged(const QString &path);

private:
    void loadDefaults();
    bool parseJsonFile(const QString &path);

    static QString canonicalize(const QString &path);

    QString m_name;
    QString m_rootDir;
    QString m_jsonPath;

    QHash<QString, QColor>   m_colors;
    QHash<QString, QFont>    m_fonts;
    QHash<QString, int>      m_metrics;
    QHash<QString, QString>  m_imagePaths;        // key -> relative filename
    QHash<QString, int>      m_imageInts;         // "krell.frames" -> 32
    QHash<QString, QString>  m_imageStrings;      // "panel_bg.mode" -> "stretch"
    mutable QHash<QString, QPixmap> m_pixmapCache;  // lazy-loaded; cleared on reload

    QFileSystemWatcher *m_watcher = nullptr;  // child QObject; parent owns

    Q_DISABLE_COPY_MOVE(Theme)
};
