#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
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

    QString name() const { return m_name; }
    QString rootDir() const { return m_rootDir; }

    // Load by bare theme name (no slashes). Returns true on success.
    // On failure the built-in defaults remain active.
    bool load(const QString &name);

    QColor color(const QString &key, const QColor &fallback = QColor()) const;
    QFont  font (const QString &key, const QFont  &fallback = QFont())  const;
    int    metric(const QString &key, int fallback = 0) const;

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

    static bool    isSafeThemeName(const QString &name);
    static QString canonicalize(const QString &path);

    QString m_name;
    QString m_rootDir;
    QString m_jsonPath;

    QHash<QString, QColor> m_colors;
    QHash<QString, QFont>  m_fonts;
    QHash<QString, int>    m_metrics;

    QFileSystemWatcher *m_watcher = nullptr;  // child QObject; parent owns

    Q_DISABLE_COPY_MOVE(Theme)
};
