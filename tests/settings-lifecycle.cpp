#define main krellixMain
#include "../src/krellix/main.cpp"
#undef main
#include "krellix/SettingsDialog.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLayout>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QStyleOptionComboBox>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QWindow>
#include <memory>
#include "sysdep/SensorDisplay.h"
#include "monitors/ClockMonitor.h"
#include "monitors/DiskMonitor.h"
#include "widgets/Chart.h"
#include "widgets/Decal.h"
#include "widgets/Panel.h"

static void mouse(QWidget *target, QEvent::Type type, const QPoint &position,
                  Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, target->mapFromGlobal(position), position,
                      button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

static void verifyDragHandle()
{
    Theme theme;
    MainWindow test(&theme, {QStringLiteral("host"), QStringLiteral("clock")});
    test.move(100, 100);
    test.show();
    QApplication::processEvents();
    auto *host = test.findChild<Panel *>(QStringLiteral("krellixHostnamePanel"));
    if (!host || !host->isVisible()) qFatal("Missing hostname panel drag handle");
    auto *hostLabel = host->findChild<Decal *>();
    if (!hostLabel) qFatal("Missing hostname label");
    const QPoint hostPos = hostLabel->mapToGlobal(hostLabel->rect().center());
    const QPoint original = test.pos();
    const QPoint delta(20, 15);
    for (Panel *panel : test.findChildren<Panel *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (panel == host || !panel->isVisible()) continue;
        const QPoint from = panel->mapToGlobal(panel->rect().center());
        mouse(panel, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        mouse(panel, QEvent::MouseMove, hostPos, Qt::NoButton, Qt::LeftButton);
        mouse(panel, QEvent::MouseButtonRelease, hostPos, Qt::LeftButton, Qt::NoButton);
        if (test.pos() != original) qFatal("A drag starting outside the hostname moved the window");
    }
    mouse(hostLabel, QEvent::MouseButtonPress, hostPos, Qt::LeftButton, Qt::LeftButton);
    mouse(hostLabel, QEvent::MouseMove, hostPos + delta, Qt::NoButton, Qt::LeftButton);
    mouse(hostLabel, QEvent::MouseButtonRelease, hostPos + delta, Qt::LeftButton, Qt::NoButton);
    if (test.pos() != original + delta) qFatal("Dragging the hostname label did not move the window");
    mouse(hostLabel, QEvent::MouseMove, hostPos + delta * 2, Qt::NoButton, Qt::LeftButton);
    if (test.pos() != original + delta) qFatal("Drag remained active after release");
    qInfo("PASS: only the hostname panel can start a drag in a theme without a header");
}

static void verifyThemeHeaderDragHandles()
{
    QTemporaryDir fixture;
    const QDir previous = QDir::current();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path()))
        qFatal("Could not isolate the header drag test");
    const auto writeTheme = [](const QString &name, const QByteArray &manifest) {
        const QString path = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(path)) qFatal("Could not create header fixture");
        QImage image(20, 8, QImage::Format_RGB32);
        image.fill(Qt::blue);
        if (!image.save(path + QStringLiteral("/strip.png"))) qFatal("Could not save header fixture");
        QFile file(path + QStringLiteral("/theme.json"));
        if (!file.open(QIODevice::WriteOnly) || file.write(manifest) != manifest.size())
            qFatal("Could not write header fixture");
    };
    writeTheme(QStringLiteral("drag-header"),
        R"({"metrics":{"top_strip_height":16},"surfaces":{"panel_bg_top":{"image":"strip.png"}}})");
    writeTheme(QStringLiteral("drag-frame"),
        R"({"images":{"frame_top":"strip.png","frame_left":"strip.png","frame_bottom":"strip.png"}})");
    writeTheme(QStringLiteral("drag-plain"), "{}");

    Theme theme;
    MainWindow window(&theme, {QStringLiteral("clock")});
    window.move(100, 100);
    window.show();
    const auto checkDrag = [&](QWidget *target, const QPoint &local, bool shouldMove,
                               Qt::MouseButton button = Qt::LeftButton) {
        const QPoint from = target->mapToGlobal(local);
        const QPoint origin = window.pos();
        const QPoint delta(18, 12);
        mouse(target, QEvent::MouseButtonPress, from, button, button);
        mouse(target, QEvent::MouseMove, from + delta, Qt::NoButton, button);
        mouse(target, QEvent::MouseButtonRelease, from + delta, button, Qt::NoButton);
        if (window.pos() != (shouldMove ? origin + delta : origin))
            qFatal("Unexpected drag result for %s at (%d, %d)", qPrintable(theme.name()), local.x(), local.y());
        mouse(target, QEvent::MouseMove, from + delta * 2, Qt::NoButton, button);
        if (window.pos() != (shouldMove ? origin + delta : origin))
            qFatal("Header drag remained active after release");
    };

    if (!theme.load(QStringLiteral("drag-header"))) qFatal("Could not load header fixture");
    QApplication::processEvents();
    auto *header = window.layout()->itemAt(0)->widget();
    if (!header || !header->isVisible() || header->height() != 16) qFatal("Top strip missing");
    checkDrag(header, header->rect().center(), true);
    checkDrag(header, header->rect().center(), false, Qt::RightButton);

    if (!theme.load(QStringLiteral("drag-frame"))) qFatal("Could not load frame fixture");
    QApplication::processEvents();
    if (header->isVisible() || window.layout()->contentsMargins().top() != 8)
        qFatal("Top frame fixture missing");
    checkDrag(&window, QPoint(window.width() / 2, 4), true);
    checkDrag(&window, QPoint(2, window.height() / 2), false);
    checkDrag(&window, QPoint(window.width() / 2, window.height() - 2), false);

    if (!theme.load(QStringLiteral("drag-plain"))) qFatal("Could not load plain fixture");
    QApplication::processEvents();
    if (header->isVisible() || window.layout()->contentsMargins().top() != 0)
        qFatal("Header did not disappear on theme switch");
    checkDrag(&window, QPoint(window.width() / 2, 4), false);
    if (!QDir::setCurrent(previous.absolutePath())) qFatal("Could not restore working directory");
    qInfo("PASS: theme strip and frame top can start a drag without a hostname, but side and bottom frames cannot");
}

static int completed = 0;

static void verifySharedHostClockCasing()
{
    Theme theme;
    MainWindow window(&theme, {QStringLiteral("host"), QStringLiteral("clock")});
    window.show();
    QApplication::processEvents();
    const auto panels = window.findChildren<Panel *>(QString(), Qt::FindDirectChildrenOnly);
    const auto shared = [&] {
        int count = 0;
        for (const auto *panel : panels)
            if (panel->property("krellSharedPanelCasing").toBool()) ++count;
        return count;
    };
    const int height = window.height();
    if (shared() != 0) qFatal("Legacy theme grouped the host and clock");
    theme.setMetric(QStringLiteral("host_clock_shared_casing"), 1);
    QApplication::processEvents();
    if (shared() != 2 || window.height() != height)
        qFatal("Shared casing did not preserve both monitor layouts");
    theme.setMetric(QStringLiteral("host_clock_shared_casing"), 0);
    QApplication::processEvents();
    if (shared() != 0 || window.height() != height)
        qFatal("Switching off shared casing did not restore the separate panels");
    theme.setMetric(QStringLiteral("host_clock_shared_casing"), 1);
    MainWindow clockOnly(&theme, {QStringLiteral("clock")});
    clockOnly.show();
    QApplication::processEvents();
    for (const auto *panel : clockOnly.findChildren<Panel *>())
        if (panel->property("krellSharedPanelCasing").toBool())
            qFatal("A standalone clock was stripped of its casing");
}

static void verifySavedHeightWinsOverTheme()
{
    QTemporaryDir fixture;
    const auto previousDir = QDir::currentPath();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path()))
        qFatal("Could not isolate preferred heights");
    const auto manifest = [](QString name, int height) {
        const auto path = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(path)) qFatal("Cannot create size fixture");
        QFile file(path + QStringLiteral("/theme.json"));
        const auto json = QByteArray(R"({"metrics":{"chart_height":)") + QByteArray::number(height) + "}}";
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size())
            qFatal("Cannot write size fixture");
        return QFileInfo(file).canonicalFilePath();
    };
    manifest(QStringLiteral("tall"), 100);
    manifest(QStringLiteral("short"), 32);
    const auto key = QStringLiteral("appearance/chart_height");
    QSettings settings;
    const auto previous = settings.value(key);
    settings.remove(key);
    {
        Theme withoutSavedHeight;
        if (!withoutSavedHeight.load(QStringLiteral("tall"))) qFatal("Tall theme missing");
        MainWindow defaultWindow(&withoutSavedHeight, {QStringLiteral("clock")});
        Chart chart(&withoutSavedHeight);
        if (chart.sizeHint().height() != 32 || settings.contains(key))
            qFatal("Theme controlled chart height for a profile without a saved value");
        if (!QMetaObject::invokeMethod(&defaultWindow, "showSettings", Qt::DirectConnection))
            qFatal("Could not open height settings");
        auto *height = defaultWindow.findChild<QSpinBox *>(QStringLiteral("chart-height"));
        if (!height || height->value() != 32) qFatal("Settings do not show the profile default");
        height->setValue(56);
        if (settings.value(key).toInt() != 56 || chart.sizeHint().height() != 56)
            qFatal("Settings did not apply the chart height live");
    }
    settings.setValue(key, 80);
    Theme theme;
    if (!theme.load(QStringLiteral("tall"))) qFatal("Tall theme missing");
    MainWindow window(&theme, {QStringLiteral("clock")});
    Chart chart(&theme);
    if (chart.sizeHint().height() != 80) qFatal("Saved chart height not applied at startup");
    if (!theme.load(QStringLiteral("short")) || chart.sizeHint().height() != 80)
        qFatal("Theme selection replaced the saved chart height");
    const auto path = manifest(QStringLiteral("short"), 144);
    if (!QMetaObject::invokeMethod(&theme, "onFileChanged", Qt::DirectConnection, Q_ARG(QString, path))
        || chart.sizeHint().height() != 80)
        qFatal("Live theme reload replaced the saved chart height");
    Theme fresh;
    if (!fresh.load(QStringLiteral("short")) || fresh.metric(QStringLiteral("chart_height")) != 144)
        qFatal("Standalone theme metrics changed");
    if (previous.isValid()) settings.setValue(key, previous);
    else settings.remove(key);
    if (!QDir::setCurrent(previousDir)) qFatal("Could not restore working directory");
}

static void verifyAppearanceSelector(SettingsDialog &dialog, QComboBox &combo, const QString &pluginName)
{
    const auto select = [&](const QString &listName, const QString &label) {
        auto *list = dialog.findChild<QListWidget *>(listName);
        if (!list) qFatal("Settings lists missing");
        const auto items = list->findItems(label, Qt::MatchExactly);
        if (items.isEmpty()) qFatal("Could not open the plugin Settings page");
        list->setCurrentItem(items.first());
    };
    select(QStringLiteral("settings-page-list"), QStringLiteral("Plugins"));
    select(QStringLiteral("plugin-settings-list"), pluginName);
    const QSize initialSize = dialog.size();
    const int initialIndex = combo.currentIndex();
    dialog.show();
    for (int width : {initialSize.width(), dialog.minimumWidth()}) {
        dialog.resize(width, initialSize.height());
        QApplication::processEvents();
        if (!combo.isVisible()) qFatal("Appearance selector is not visible");
        for (int i = 0; i < combo.count(); ++i) {
            combo.setCurrentIndex(i);
            QStyleOptionComboBox option;
            option.initFrom(&combo);
            option.subControls = QStyle::SC_All;
            option.currentText = combo.currentText();
            const auto rect = combo.style()->subControlRect(QStyle::CC_ComboBox, &option,
                                                            QStyle::SC_ComboBoxEditField, &combo);
            if (rect.width() < combo.fontMetrics().horizontalAdvance(option.currentText)
                || rect.height() < combo.fontMetrics().height()) {
                qFatal("Appearance selection clips at dialog width %d: %s, field %dx%d", width,
                       qPrintable(option.currentText), rect.width(), rect.height());
            }
        }
        combo.showPopup();
        QApplication::processEvents();
        auto *view = combo.view();
        if (!view->isVisible() || view->textElideMode() != Qt::ElideNone)
            qFatal("Appearance selector must open a popup that shows the complete labels");
        for (int i = 0; i < combo.count(); ++i) {
            if (view->viewport()->width() < view->sizeHintForIndex(combo.model()->index(i, 0)).width())
                qFatal("Appearance popup clips %s", qPrintable(combo.itemText(i)));
        }
        combo.hidePopup();
    }
    combo.setCurrentIndex(initialIndex);
}

static void verifyMailAnimationSetting(Theme *theme)
{
    QTemporaryDir fixture;
    const QByteArray devPlugins = qgetenv("KRELLIX_DEV_PLUGINS");
    const QString previous = QDir::currentPath();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path()) || !QDir().mkdir(QStringLiteral("plugins")))
        qFatal("Cannot prepare KrellMail settings fixture");
    QFile installed(QStringLiteral("plugins/krellmail.so"));
    if (!installed.open(QIODevice::WriteOnly)) qFatal("Cannot mark KrellMail installed");
    installed.close();
    QFile weather(QStringLiteral("plugins/krellweather.so"));
    if (!weather.open(QIODevice::WriteOnly)) qFatal("Cannot mark KrellWeather installed");
    weather.close();
    qputenv("KRELLIX_DEV_PLUGINS", "1");
    const auto key = QStringLiteral("plugins/krellmail/icon_style");
    const auto weatherKey = QStringLiteral("plugins/krellweather/icon_mode");
    QSettings settings;
    settings.remove(key);
    {
        SettingsDialog dialog(theme, nullptr);
        auto *style = dialog.findChild<QComboBox *>(QStringLiteral("krellmail-icon-style"));
        auto *interval = dialog.findChild<QSpinBox *>(QStringLiteral("krellmail-update-interval"));
        if (!interval || interval->value() != 60000) qFatal("KrellMail settings show the wrong default interval");
        if (!style || style->count() != 3 || style->currentData().toString() != QLatin1String("envelope"))
            qFatal("KrellMail animation choices are missing from settings");
        verifyAppearanceSelector(dialog, *style, QStringLiteral("Krellmail"));
        const int tux = style->findData(QStringLiteral("tux"));
        if (tux < 0) qFatal("Missing Tux animation choice");
        style->setCurrentIndex(tux);
        int applied = 0;
        QObject::connect(&dialog, &SettingsDialog::settingsApplied, &dialog, [&] { ++applied; });
        if (!QMetaObject::invokeMethod(style, "activated", Qt::DirectConnection, Q_ARG(int, tux))
            || applied != 1 || settings.value(key).toString() != QLatin1String("tux"))
            qFatal("KrellMail animation choice did not apply");
    }
    {
        SettingsDialog dialog(theme, nullptr);
        auto *style = dialog.findChild<QComboBox *>(QStringLiteral("krellmail-icon-style"));
        if (!style || style->currentData().toString() != QLatin1String("tux"))
            qFatal("KrellMail animation choice was lost when reopening settings");

        auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("krellweather-icon-mode"));
        if (!mode || mode->count() != 3 || mode->currentData().toString() != QLatin1String("color"))
            qFatal("Weather icon modes or default are missing");
        verifyAppearanceSelector(dialog, *mode, QStringLiteral("Krellweather"));
        for (const auto &value : {QStringLiteral("theme"), QStringLiteral("mono")}) {
            const int index = mode->findData(value);
            if (index < 0 || !QMetaObject::invokeMethod(mode, "activated", Qt::DirectConnection, Q_ARG(int, index))
                || settings.value(weatherKey).toString() != value)
                qFatal("Weather icon mode did not persist");
        }
    }
    {
        SettingsDialog dialog(theme, nullptr);
        const auto *mode = dialog.findChild<QComboBox *>(QStringLiteral("krellweather-icon-mode"));
        if (!mode || mode->currentData().toString() != QLatin1String("mono"))
            qFatal("Weather icon mode was lost when reopening settings");
    }
    settings.remove(key);
    settings.remove(weatherKey);
    if (devPlugins.isNull()) qunsetenv("KRELLIX_DEV_PLUGINS");
    else qputenv("KRELLIX_DEV_PLUGINS", devPlugins);
    if (!QDir::setCurrent(previous)) qFatal("Cannot restore working directory");
    qInfo("PASS: KrellMail animation preference is saved and restored in configuration");
}

static void verifyClockScroll(Theme *theme)
{
    ClockMonitor clock(theme);
    std::unique_ptr<QWidget> panel(clock.createWidget(nullptr));
    const auto labels = panel->findChildren<Decal *>(QString(), Qt::FindDirectChildrenOnly);
    if (labels.size() != 2 || labels.first()->text().isEmpty()) qFatal("Clock text missing");
    auto *timer = labels.first()->findChild<QTimer *>();
    if (!timer) qFatal("Clock scroll timer missing");

    panel->setFixedWidth(400);
    panel->show();
    QApplication::processEvents();
    if (timer->isActive()) qFatal("Clock scrolled even though it fit");

    panel->setFixedWidth(20);
    QApplication::processEvents();
    if (!timer->isActive()) qFatal("Clock did not scroll when it overflowed");

    panel->setFixedWidth(400);
    QApplication::processEvents();
    if (timer->isActive()) qFatal("Clock kept scrolling after it fit again");
    qInfo("PASS: clock scrolls only on overflow");
}

static void verifyFooterFrame()
{
    QTemporaryDir fixture;
    const auto current = QDir::currentPath();
    const auto path = fixture.path() + QStringLiteral("/themes/footer-frame-test");
    if (!fixture.isValid() || !QDir().mkpath(path) || !QDir::setCurrent(fixture.path()))
        qFatal("Could not prepare footer theme test");

    QImage footer(100, 20, QImage::Format_RGB32);
    footer.fill(Qt::red);
    for (int y = 0; y < footer.height(); ++y)
        for (int x = 50; x < footer.width(); ++x)
            footer.setPixelColor(x, y, Qt::blue);
    if (!footer.save(path + QStringLiteral("/footer.png"))) qFatal("Could not save footer artwork");

    QSettings settings;
    const auto widthKey = QStringLiteral("appearance/panel_width");
    const QVariant savedWidth = settings.value(widthKey);
    for (bool stretch : {false, true}) {
        QFile json(path + QStringLiteral("/theme.json"));
        const QByteArray data = stretch
            ? R"({"images":{"frame_bottom":{"image":"footer.png","mode":"stretch"}}})"
            : R"({"images":{"frame_bottom":"footer.png"}})";
        if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
            qFatal("Could not write footer theme test");
        json.close();
        Theme theme;
        if (!theme.load(QStringLiteral("footer-frame-test"))) qFatal("Could not load footer theme test");
        for (int width : {120, 240}) {
            settings.setValue(widthKey, width);
            MainWindow window(&theme, {QStringLiteral("clock")});
            window.show();
            QApplication::processEvents();
            const int expected = stretch ? width / 5 : footer.height();
            if (window.layout()->contentsMargins().bottom() != expected)
                qFatal("Footer height did not match its frame mode");
            const auto image = window.grab().toImage();
            const auto color = image.pixelColor(image.width() - 5, image.height() - 5);
            if (color != (stretch ? QColor(Qt::blue) : QColor(Qt::red)))
                qFatal("Footer did not paint in its configured frame mode");
        }
    }
    if (savedWidth.isValid()) settings.setValue(widthKey, savedWidth);
    else settings.remove(widthKey);
    if (!QDir::setCurrent(current)) qFatal("Could not restore test working directory");
    qInfo("PASS: proportional footer scales at 120 and 240; existing frames still tile");
}

static void verifyWidthOnlyResize()
{
    QTemporaryDir fixture;
    const QDir previous = QDir::current();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path())
        || !QDir().mkpath(QStringLiteral("themes/width-only")))
        qFatal("Could not prepare width fixture");
    const QString path = QStringLiteral("themes/width-only/");
    QImage artwork(240, 80, QImage::Format_RGB32);
    artwork.fill(Qt::blue);
    if (!artwork.save(path + QStringLiteral("frame.png"))) qFatal("Could not save width fixture artwork");
    const QByteArray manifest = R"({"metrics":{"top_strip_height":80,"top_strip_keep_aspect":1},
        "surfaces":{"panel_bg_top":{"image":"frame.png"}},
        "images":{"frame_bottom":{"image":"frame.png","mode":"stretch"}}})";
    QFile json(path + QStringLiteral("theme.json"));
    if (!json.open(QIODevice::WriteOnly) || json.write(manifest) != manifest.size())
        qFatal("Could not write width fixture");
    json.close();

    QSettings settings;
    const auto widthKey = QStringLiteral("appearance/panel_width");
    const auto referenceKey = QStringLiteral("appearance/frame_reference_width");
    const QVariant savedWidth = settings.value(widthKey);
    const QVariant savedReference = settings.value(referenceKey);
    settings.setValue(widthKey, 120);
    settings.remove(referenceKey);
    const auto verify = [](const MainWindow &window, int width, int height) {
        const auto *header = window.layout()->itemAt(0)->widget();
        if (window.width() != width || window.height() != height
            || !header || header->height() != 40 || window.layout()->contentsMargins().bottom() != 40)
            qFatal("Width change to %d altered the established vertical geometry", width);
    };
    int originalHeight = 0;
    {
        Theme theme;
        if (!theme.load(QStringLiteral("width-only"))) qFatal("Could not load width fixture");
        MainWindow window(&theme, {QStringLiteral("clock")});
        window.show();
        QApplication::processEvents();
        originalHeight = window.height();
        verify(window, 120, originalHeight);
        Chart chart(&theme);
        const int chartHeight = chart.sizeHint().height();
        if (!QMetaObject::invokeMethod(&window, "showSettings", Qt::DirectConnection))
            qFatal("Could not open width settings");
        auto *widthControl = window.findChild<QSpinBox *>(QStringLiteral("panel-width"));
        if (!widthControl || widthControl->value() != 120) qFatal("Missing width setting");
        for (int width : {240, 90, 320}) {
            widthControl->setValue(width);
            QApplication::processEvents();
            verify(window, width, originalHeight);
            if (chart.sizeHint().height() != chartHeight)
                qFatal("Width was applied to the chart height");
        }
        if (!theme.load(QStringLiteral("width-only"))) qFatal("Could not reload width fixture");
        QApplication::processEvents();
        verify(window, 320, originalHeight);
    }
    settings.sync();
    if (settings.value(referenceKey).toInt() != 120) qFatal("Frame sizing reference was not saved");
    {
        Theme theme;
        if (!theme.load(QStringLiteral("width-only"))) qFatal("Could not load restart fixture");
        MainWindow reopened(&theme, {QStringLiteral("clock")});
        reopened.show();
        QApplication::processEvents();
        verify(reopened, 320, originalHeight);
    }
    if (savedWidth.isValid()) settings.setValue(widthKey, savedWidth);
    else settings.remove(widthKey);
    if (savedReference.isValid()) settings.setValue(referenceKey, savedReference);
    else settings.remove(referenceKey);
    if (!QDir::setCurrent(previous.absolutePath())) qFatal("Could not restore the working directory");
    qInfo("PASS: changing only width preserves the header, footer, chart, and window heights after reload and restart");
}

static void verifyThemeWindowShadow()
{
    QTemporaryDir fixture;
    const auto current = QDir::currentPath();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path())) qFatal("Could not isolate shadow test");
    const auto writeTheme = [](const QString &name, const QByteArray &data) {
        const QString directory = QStringLiteral("themes/") + name;
        if (!QDir().mkpath(directory)) qFatal("Could not create shadow theme");
        QFile json(directory + QStringLiteral("/theme.json"));
        if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
            qFatal("Could not write shadow theme");
    };
    const auto flat = QStringLiteral("flat-window");
    const auto normal = QStringLiteral("native-shadow");
    writeTheme(flat, R"({"metrics":{"window_shadow":0}})");
    writeTheme(normal, R"({})");
    Theme theme;
    if (!theme.load(flat)) qFatal("Could not load shadowless theme");
    MainWindow window(&theme, {QStringLiteral("host")});
    window.move(100, 100);
    window.show();
    QApplication::processEvents();
    const auto hasNoShadow = [&] {
        return window.windowHandle() && window.windowHandle()->flags().testFlag(Qt::NoDropShadowWindowHint);
    };
    if (!hasNoShadow()) qFatal("Theme shadow opt-out missing on startup");
    const auto original = window.geometry();
    if (!theme.load(normal) || hasNoShadow() || !window.isVisible() || window.geometry() != original)
        qFatal("Switching themes did not restore the default shadow without moving or hiding the window");
    if (!theme.load(flat) || !hasNoShadow() || !window.isVisible() || window.geometry() != original)
        qFatal("Live theme switch did not suppress the shadow");
    QSettings settings;
    const auto key = QStringLiteral("window/always_on_top");
    const auto saved = settings.value(key);
    QMetaObject::invokeMethod(&window, "toggleAlwaysOnTop", Qt::DirectConnection);
    if (!hasNoShadow()) qFatal("Always-on-top change lost theme shadow preference");
    if (saved.isValid()) settings.setValue(key, saved);
    else settings.remove(key);
    if (!QDir::setCurrent(current)) qFatal("Could not restore shadow test working directory");
    qInfo("PASS: window shadow opt-out applies at startup and live without changing default themes");
}

static void verifyChassisBackground()
{
    QTemporaryDir fixture;
    const auto previous = QDir::currentPath();
    if (!fixture.isValid() || !QDir::setCurrent(fixture.path())) qFatal("Could not isolate chassis test");
    for (const auto &id : {QStringLiteral("chassis-base"), QStringLiteral("chassis-textured"),
                           QStringLiteral("chassis-capped")}) {
        const auto path = QStringLiteral("themes/") + id + QLatin1Char('/');
        if (!QDir().mkpath(path)) qFatal("Could not create chassis theme");
        const auto save = [&](const QString &name, QSize size, QColor color) {
            QImage image(size, QImage::Format_ARGB32);
            image.fill(color);
            if (!image.save(path + name)) qFatal("Could not save chassis fixture");
        };
        save(QStringLiteral("header.png"), QSize(20, 10), Qt::yellow);
        save(QStringLiteral("footer.png"), QSize(20, 8), Qt::cyan);
        save(QStringLiteral("spacer.png"), QSize(6, 8), Qt::transparent);
        QImage pattern(5, 3, QImage::Format_RGB32);
        pattern.fill(Qt::red);
        for (int x = 0; x < pattern.width(); ++x) pattern.setPixelColor(x, 1, Qt::blue);
        if (!pattern.save(path + QStringLiteral("pattern.png"))) qFatal("Could not save chassis backing");
        QImage cap(20, 6, QImage::Format_ARGB32);
        cap.fill(Qt::magenta);
        for (const auto &pt : {QPoint(0, 0), QPoint(19, 0), QPoint(0, 5), QPoint(19, 5)})
            cap.setPixelColor(pt, Qt::transparent);
        if (!cap.save(path + QStringLiteral("cap.png"))) qFatal("Could not save chassis cap");
        const QByteArray data = R"({"metrics":{"top_strip_height":14,"panel_border":0},
          "colors":{"panel_bg":"#101010"},"surfaces":{"panel_bg_top":{"image":"header.png","slice":0}},
          "images":{"frame_left":"spacer.png","frame_right":"spacer.png","frame_bottom":"footer.png")"
          + QByteArray(id != QStringLiteral("chassis-base") ? R"(,"window_bg":"pattern.png")" : "")
          + QByteArray(id == QStringLiteral("chassis-capped")
              ? R"(,"window_bg_cap_top":"cap.png","window_bg_cap_bottom":"cap.png")" : "") + "}}";
        QFile json(path + QStringLiteral("theme.json"));
        if (!json.open(QIODevice::WriteOnly) || json.write(data) != data.size())
            qFatal("Could not write chassis theme");
    }
    Theme theme;
    if (!theme.load(QStringLiteral("chassis-base"))) qFatal("Could not load plain chassis");
    MainWindow window(&theme, {QStringLiteral("clock")});
    window.show();
    QApplication::processEvents();
    const auto before = window.grab().toImage();
    const auto margins = window.layout()->contentsMargins();
    const auto content = window.layout()->itemAt(window.layout()->count() - 1)->geometry();
    if (!theme.load(QStringLiteral("chassis-textured"))) qFatal("Could not load textured chassis");
    QApplication::processEvents();
    const auto after = window.grab().toImage();
    const auto dpr = after.devicePixelRatio();
    const auto sample = [dpr](const QImage &image, int x, int y) {
        return image.pixelColor(qFloor((x + .5)*dpr), qFloor((y + .5)*dpr));
    };
    const auto matchesTile = [&](int x, int y, QColor expected) {
        const auto actual = sample(after, x, y);
        return qAbs(actual.red() - expected.red()) <= 2 && qAbs(actual.green() - expected.green()) <= 2
            && qAbs(actual.blue() - expected.blue()) <= 2;
    };
    if (before.size() != after.size() || margins != window.layout()->contentsMargins()
        || content != window.layout()->itemAt(window.layout()->count() - 1)->geometry()
        || sample(before, 2, 16) != QColor("#101010")
        || !matchesTile(2, 16, Qt::red) || !matchesTile(2, 17, Qt::red)
        || !matchesTile(2, 18, Qt::blue) || !matchesTile(window.width() - 2, 18, Qt::blue)
        || sample(after, 15, 3) != Qt::yellow || sample(after, 15, window.height() - 2) != Qt::cyan
        || sample(after, content.left() + 1, content.top() + 1) != QColor("#101010"))
        qFatal("Chassis geometry/paint mismatch: layout=%d rows=%s/%s,%s,%s,%s header=%s footer=%s face=%s",
               int(content == window.layout()->itemAt(window.layout()->count() - 1)->geometry()),
               qPrintable(sample(before, 2, 16).name()), qPrintable(sample(after, 2, 16).name()),
               qPrintable(sample(after, 2, 17).name()), qPrintable(sample(after, 2, 18).name()),
               qPrintable(sample(after, window.width() - 2, 18).name()),
               qPrintable(sample(after, 15, 3).name()), qPrintable(sample(after, 15, window.height() - 2).name()),
               qPrintable(sample(after, content.left() + 1, content.top() + 1).name()));
    if (!theme.load(QStringLiteral("chassis-capped"))) qFatal("Could not load capped chassis");
    QApplication::processEvents();
    const auto capped = window.grab().toImage();
    if (capped.size() != after.size() || margins != window.layout()->contentsMargins()
        || content != window.layout()->itemAt(window.layout()->count() - 1)->geometry()
        || sample(capped, 0, 0).alpha() != 0 || sample(capped, window.width() - 1, window.height() - 1).alpha() != 0
        || sample(capped, 2, 2) != Qt::magenta || sample(capped, window.width() - 2, window.height() - 2) != Qt::magenta
        || sample(capped, 15, 3) != Qt::yellow || sample(capped, 15, window.height() - 2) != Qt::cyan
        || sample(capped, 2, 18) != sample(after, 2, 18))
        qFatal("Chassis cap mismatch: alphas=%d/%d cap=%s/%s header=%s footer=%s backdrop=%s",
               sample(capped, 0, 0).alpha(), sample(capped, window.width() - 1, window.height() - 1).alpha(),
               qPrintable(sample(capped, 2, 2).name()), qPrintable(sample(capped, window.width() - 2, window.height() - 2).name()),
               qPrintable(sample(capped, 15, 3).name()), qPrintable(sample(capped, 15, window.height() - 2).name()),
               qPrintable(sample(capped, 2, 18).name()));
    if (!QDir::setCurrent(previous)) qFatal("Could not restore working directory");
}

static void exercise()
{
    MainWindow *window = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (auto *candidate = qobject_cast<MainWindow *>(widget)) window = candidate;
    if (!window) qFatal("Main window missing");
#ifdef Q_OS_MACOS
    if (!isMacAccessoryApplication()) qFatal("Mac monitor unexpectedly appears in Dock");
    if (window->windowType() != Qt::Window) qFatal("Main window must remain a normal Mac window");
#endif
    static bool diskChecked = false;
    if (!diskChecked) {
        diskChecked = true;
        auto *existing = window->findChild<MonitorBase *>();
        if (!existing) qFatal("No monitor for disk test theme");
        verifyDragHandle();
        verifyThemeHeaderDragHandles();
        verifySharedHostClockCasing();
        verifySavedHeightWinsOverTheme();
        verifyClockScroll(existing->theme());
        verifyMailAnimationSetting(existing->theme());
        verifyFooterFrame();
        verifyWidthOnlyResize();
        verifyThemeWindowShadow();
        verifyChassisBackground();
        DiskMonitor disk(existing->theme());
        DiskStat::setReadOverride([]() { return QList<DiskSample>{}; });
        QWidget *panel = disk.createWidget(nullptr);
        DiskStat::setReadOverride([]() { DiskSample s; s.name = QStringLiteral("vda"); s.sectorsRead = 100; return QList<DiskSample>{s}; });
        disk.tick();
        for (auto *label : panel->findChildren<Decal *>())
            if (label->text().contains(QStringLiteral("no whole disks"))) qFatal("Disk panel stayed empty after remote sample");
        const auto *chart = panel->findChild<Chart *>();
        if (!chart || !chart->accessibleName().startsWith(QStringLiteral("R ")))
            qFatal("Remote disk chart readout never appeared");
        DiskStat::setReadOverride([]() { return QList<DiskSample>{}; });
        disk.tick();
        bool foundEmpty = false;
        for (auto *label : panel->findChildren<Decal *>())
            if (label->text().contains(QStringLiteral("no whole disks"))) foundEmpty = true;
        if (!foundEmpty || panel->findChild<Chart *>()) qFatal("Disconnected disk left stale panel");
        delete panel;
        DiskStat::setReadOverride(nullptr);
    }
    static bool moved = false;
    if (!moved) {
        moved = true;
        window->move(42, 73);
        QTimer::singleShot(400, qApp, exercise);
        return;
    }
    if (QSettings().value(QStringLiteral("window/pos")).toPoint() != QPoint(42, 73))
        qFatal("Window position was not saved before closing");
    if (completed == 3) {
        if (!window->isVisible()) qFatal("Main window disappeared");
        window->close(); // Explicit main-window close must still exit.
        return;
    }
    QMetaObject::invokeMethod(window, "showSettings", Qt::DirectConnection);
    auto *dialog = window->findChild<SettingsDialog *>();
    if (!dialog) qFatal("Settings missing");
    const QSize available = window->screen()->availableGeometry().size();
    const QSize expected = QSize(920, 740).boundedTo(available - QSize(24, 56));
    if (dialog->width() < expected.width() || dialog->height() < expected.height()
        || dialog->width() > available.width() || dialog->height() > available.height())
        qFatal("Settings did not open at the larger size within the screen");
    if (dialog->isModal() || dialog->windowModality() != Qt::NonModal)
        qFatal("Settings must not attach as a modal sheet");
#ifdef Q_OS_WIN
    if (completed == 0) {
        auto *monitor = window->findChild<MonitorBase *>();
        if (!monitor) qFatal("No monitor for scroll test theme");
        Decal label(monitor->theme(), QStringLiteral("label"), QStringLiteral("text_primary"));
        label.resize(32, 24);
        label.show();
        auto *timer = label.findChild<QTimer *>();
        label.setText(QStringLiteral("i"));
        if (!timer || timer->isActive()) qFatal("Text that fits should not scroll");
        label.setText(QStringLiteral("A long label that cannot fit in thirty-two pixels"));
        if (!timer->isActive()) qFatal("Overflowing text did not start scrolling");
        QSettings().setValue(QStringLiteral("appearance/scroll_fps"), 20);
        QMetaObject::invokeMethod(dialog, "settingsApplied", Qt::DirectConnection);
        QMetaObject::invokeMethod(&label, "onScrollTick", Qt::DirectConnection);
        if (timer->interval() != 50) qFatal("Live scroll settings were not applied");
        label.hide();
        if (timer->isActive()) qFatal("Hidden text should not keep scrolling");
    }
#endif
    const QPoint original = window->pos();
    dialog->move(dialog->pos() + QPoint(30, 40));
    if (window->pos() != original) qFatal("Moving settings moved the monitor");
    QPointer<SettingsDialog> guard(dialog);
    QTimer::singleShot(30, dialog, [dialog]() {
        if (completed == 0) dialog->close();
        else if (completed == 1) dialog->reject(); // Escape path
        else dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Close)->click();
    });
    QTimer::singleShot(100, qApp, [guard]() {
        if (guard) qFatal("Settings dialog was not destroyed");
        ++completed;
        exercise();
    });
}
static void scheduleTest() { QTimer::singleShot(100, exercise); }
Q_COREAPP_STARTUP_FUNCTION(scheduleTest)

int main(int argc, char **argv)
{
    SensorReading a, b, disk;
    a.chip = b.chip = QStringLiteral("coretemp");
    a.label = QStringLiteral("Core 0"); a.value = 50;
    b.label = QStringLiteral("Core 1"); b.value = 80; b.critC = 100;
    disk.chip = QStringLiteral("nvme"); disk.label = QStringLiteral("Composite"); disk.value = 40;
    const QList<SensorReading> input{a, b, disk};
    const auto compact = sensorDisplayReadings(input, false);
    if (compact.size() != 2 || compact[0].value != 80 || compact[0].critC != 100
        || compact[0].label != QStringLiteral("CPU") || compact[1].value != 40)
        qFatal("Compact sensors lost maximum temperature or mixed chip readings");
    if (sensorDisplayReadings(input, true).size() != 3
        || !sensorDisplayReadings({}, false).isEmpty()) qFatal("Detailed/empty sensor regression");
    QTemporaryDir config;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, config.path());
    const int result = krellixMain(argc, argv);
    if (completed != 3) {
        qCritical("FAIL: application exited after closing settings (%d/3 completed)", completed);
        return 1;
    }
    QSettings base(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix"));
    base.setValue(QStringLiteral("theme/name"), QStringLiteral("base"));
    base.sync();
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"), QStringLiteral("krellix"));
    QSettings first(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"));
    first.setValue(QStringLiteral("theme/name"), QStringLiteral("custom-a"));
    first.sync();
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-b"), QStringLiteral("krellix"));
    migrateLegacySettings(QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"), QStringLiteral("krellix"));
    QSettings reopened(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-a"));
    QSettings second(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("isolation-test"), QStringLiteral("krellix-host-b"));
    if (reopened.value(QStringLiteral("theme/name")).toString() != QStringLiteral("custom-a")
        || second.value(QStringLiteral("theme/name")).toString() != QStringLiteral("base")) qFatal("Instance settings leaked or were overwritten");
    qInfo("PASS: settings lifecycle and independent persistent instance settings");
    return result;
}
