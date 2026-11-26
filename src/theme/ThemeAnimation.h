#pragma once

#include "Theme.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>

#include <cmath>

// Integrate a smoothed speed, not the number of timer callbacks. Delayed
// paints and changing load must not change the cycle length or jump frames.
struct ThemePhase {
    double position = 0;
    double speed = 1;

    int advance(double seconds, double fps, int frames, double target, double smoothing)
    {
        double distance = target * seconds;
        if (smoothing > 0) {
            const double decay = std::exp(-seconds / smoothing);
            distance += (speed - target) * smoothing * (1 - decay);
            speed = target + (speed - target) * decay;
        } else {
            speed = target;
        }
        position = std::fmod(position + fps * distance, frames);
        return static_cast<int>(position);
    }
};

// Owned by one Theme (one main window). Consumers register only their
// painted regions; no widget needs its own clock.
class ThemeAnimation
{
public:
    explicit ThemeAnimation(Theme &theme) : m_theme(theme)
    {
        m_timer.setParent(&theme);
        m_timer.setObjectName(QStringLiteral("theme-frame-clock"));
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, &m_timer, [this] { tick(); });
    }

    bool load(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 32768) return false;
        const auto document = QJsonDocument::fromJson(file.read(32769));
        if (!document.isObject()) return false;
        const auto root = document.object();
        if (root.value(QStringLiteral("version")).toDouble(-1) != 1) return false;
        const auto entries = root.value(QStringLiteral("animations")).toObject();
        if (entries.isEmpty() || entries.size() > 16) return false;
        // Bound total decoded pixels, including the unusually wide PNG strips.
        qint64 budget = 8 * 1024 * 1024;
        for (const auto &value : entries) {
            const auto entry = value.toObject();
            const auto target = entry.value(QStringLiteral("target")).toString();
            if (!supportedTarget(target) || m_indices.contains(target)) continue;
            const auto trigger = entry.value(QStringLiteral("trigger")).toString();
            if (trigger != (target == QStringLiteral("mail.new") ? QStringLiteral("new_mail")
                                                                 : QStringLiteral("continuous"))) continue;
            if (entry.value(QStringLiteral("layout")).toString() != QLatin1String("horizontal")
                || !entry.value(QStringLiteral("loop")).toBool()) continue;
            const int width = integer(entry, QStringLiteral("frame_width"), 1, 1024);
            const int height = integer(entry, QStringLiteral("frame_height"), 1, 1024);
            const int count = integer(entry, QStringLiteral("frames"), 2, 120);
            const auto fps = entry.value(QStringLiteral("fps")).toDouble(-1);
            if (!width || !height || !count || !std::isfinite(fps) || fps < 1 || fps > 30) continue;
            const QSize expected(width * count, height);
            const qint64 pixels = qint64(expected.width()) * expected.height();
            if (pixels > budget || expected.width() > 16384) continue;

            const auto sheet = m_theme.assetPath(entry.value(QStringLiteral("sheet")).toString());
            if (sheet.isEmpty() || m_theme.assetPath(entry.value(QStringLiteral("fallback")).toString()).isEmpty())
                continue;
            QFile compressed(sheet);
            if (compressed.size() <= 0 || compressed.size() > 10 * 1024 * 1024) continue;
            QImageReader reader(sheet, QByteArrayLiteral("png"));
            if (reader.size() != expected) continue;
            const QImage image = reader.read();
            if (image.size() != expected) continue;

            Effect effect;
            effect.fps = fps;
            const auto rate = entry.value(QStringLiteral("speed")).toObject();
            if (target == QLatin1String("surfaces.panel_bg_top")
                && rate.value(QStringLiteral("metric")).toString() == QLatin1String("cpu_total_percent")) {
                const double low = rate.value(QStringLiteral("min_multiplier")).toDouble(-1);
                const double high = rate.value(QStringLiteral("max_multiplier")).toDouble(-1);
                const double seconds = rate.value(QStringLiteral("smoothing_seconds")).toDouble(-1);
                if (low >= 0.25 && low <= 1 && high >= 1 && high <= 2 && seconds >= 0.25 && seconds <= 10) {
                    effect.low = low;
                    effect.high = high;
                    effect.smoothing = seconds;
                }
            }
            effect.frames.reserve(count);
            bool valid = true;
            for (int i = 0; i < count; ++i) {
                auto frame = QPixmap::fromImage(image.copy(i * width, 0, width, height));
                if (frame.isNull()) {
                    valid = false;
                    break;
                }
                effect.frames.append(std::move(frame));
            }
            if (!valid) continue;
            m_indices.insert(target, int(m_effects.size()));
            m_effects.append(std::move(effect));
            budget -= pixels;
        }
        return !m_effects.isEmpty();
    }

    bool hasTarget(const QString &target) const { return m_indices.contains(target); }

    void setMode(Theme::Motion mode)
    {
        if (m_mode == mode) return;
        m_mode = mode;
        m_timer.stop();
        m_clock.invalidate();
        for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
            if (visible(it.key()))
                for (const auto &binding : it.value()) it.key()->update(binding.region);
        startIfNeeded();
    }

    void setCpu(double load)
    {
        if (!std::isfinite(load) || load < 0) {
            m_cpuAge.invalidate();
            return;
        }
        m_cpuLoad = qBound(0., load, 1.);
        m_cpuAge.start();
    }

    QPixmap frame(const QString &target, QWidget *widget, const QRect &region, QSize size)
    {
        const int index = m_indices.value(target, -1);
        if (index < 0 || !widget || region.isEmpty()) return {};
        auto &effect = m_effects[index];
        if (effect.frames.first().size() != size) return {};
        auto it = m_bindings.find(widget);
        if (it == m_bindings.end()) {
            it = m_bindings.insert(widget, {});
            QObject::connect(widget, &QObject::destroyed, &m_timer, [this, widget] {
                m_bindings.remove(widget);
            });
        }
        bool registered = false;
        for (auto &binding : it.value())
            if (binding.effect == index) {
                binding.region = region;
                registered = true;
                break;
            }
        if (!registered) it.value().append(Binding{index, region});
        if (m_mode == Theme::Motion::Off) return {};
        if (visible(widget)) startIfNeeded();
        return effect.frames.at(effect.frame);
    }

    void unwatch(const QString &target, QWidget *widget)
    {
        auto it = m_bindings.find(widget);
        if (it == m_bindings.end()) return;
        const auto index = m_indices.value(target, -1);
        it.value().removeIf([index](const Binding &b) { return b.effect == index; });
    }

private:
    struct Effect {
        QList<QPixmap> frames;
        ThemePhase phase;
        double fps = 0;
        double low = 1, high = 1, smoothing = 0;
        int frame = 0;
    };
    struct Binding { int effect; QRect region; };

    static int integer(const QJsonObject &entry, const QString &key, int low, int high)
    {
        const auto value = entry.value(key).toDouble(-1);
        if (!(value >= low && value <= high) || value != std::floor(value)) return 0;
        return static_cast<int>(value);
    }

    static bool supportedTarget(const QString &target)
    {
        return target == QLatin1String("surfaces.panel_bg_top") || target == QLatin1String("surfaces.panel_bg")
            || target == QLatin1String("surfaces.panel_bg_host")
            || target == QLatin1String("surfaces.panel_bg_host_clock") || target == QLatin1String("images.frame_left")
            || target == QLatin1String("images.frame_right") || target == QLatin1String("images.frame_bottom")
            || target == QLatin1String("images.krell") || target == QLatin1String("mail.new");
    }

    static bool visible(const QWidget *widget)
    {
        return widget->isVisible() && !widget->window()->isMinimized();
    }

    quint32 activeEffects() const
    {
        quint32 mask = 0;
        for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
            if (visible(it.key()))
                for (const auto &binding : it.value()) mask |= quint32(1) << binding.effect;
        return mask;
    }

    void startIfNeeded()
    {
        if (m_mode != Theme::Motion::Off && !m_timer.isActive() && activeEffects()) {
            m_clock.start();
            m_timer.start(m_mode == Theme::Motion::Reduced ? 125 : 16);
        }
    }

    void tick()
    {
        const auto active = activeEffects();
        if (!active || m_mode == Theme::Motion::Off) {
            m_timer.stop();
            m_clock.invalidate();
            return;
        }
        const double seconds = m_clock.nsecsElapsed() / 1e9;
        m_clock.restart();
        const bool reduced = m_mode == Theme::Motion::Reduced;
        const bool haveCpu = m_cpuAge.isValid() && m_cpuAge.elapsed() <= 15000;
        quint32 changed = 0;
        for (int i = 0; i < m_effects.size(); ++i) {
            const quint32 bit = quint32(1) << i;
            if (!(active & bit)) continue;
            auto &effect = m_effects[i];
            const double rate = !reduced && haveCpu && effect.smoothing > 0
                ? effect.low + (effect.high - effect.low) * m_cpuLoad : 1.;
            const auto frameIndex = effect.phase.advance(seconds, reduced ? qMin(4., effect.fps) : effect.fps,
                int(effect.frames.size()), rate, reduced ? 0. : effect.smoothing);
            if (frameIndex == effect.frame) continue;
            effect.frame = frameIndex;
            changed |= bit;
        }
        if (!changed) return;
        for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
            if (visible(it.key()))
                for (const auto &binding : it.value())
                    if (changed & (quint32(1) << binding.effect)) it.key()->update(binding.region);
    }

    Theme &m_theme;
    QTimer m_timer;
    QElapsedTimer m_clock, m_cpuAge;
    QHash<QString, int> m_indices;
    QList<Effect> m_effects;
    QHash<QWidget *, QList<Binding>> m_bindings;
    Theme::Motion m_mode = Theme::Motion::Full;
    double m_cpuLoad = 0;
};
