#include "AudaciousWindows.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <utility>
#include <windows.h>
#include <tlhelp32.h>

namespace {

constexpr int kMaxOutputBytes = 16 * 1024;
constexpr int kProcessDeadlineMs = 4000;
constexpr int kMaxCommands = 12;

struct WinHandle {
    HANDLE handle;
    ~WinHandle() { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

QString runningPlayer()
{
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return {};
    WinHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.handle == INVALID_HANDLE_VALUE) return {};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.handle, &entry)) return {};
    do {
        if (QString::fromWCharArray(entry.szExeFile).compare(QStringLiteral("audacious.exe"), Qt::CaseInsensitive))
            continue;
        DWORD processSession = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &processSession) || processSession != session) continue;
        WinHandle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
        if (!process.handle) continue;
        wchar_t filename[32768];
        DWORD size = sizeof(filename) / sizeof(*filename);
        if (QueryFullProcessImageNameW(process.handle, 0, filename, &size))
            return QDir::fromNativeSeparators(QString::fromWCharArray(filename, static_cast<qsizetype>(size)));
    } while (Process32NextW(snapshot.handle, &entry));
    return {};
}

QString installedPlayer()
{
    QStringList directories;
    for (const auto format : {QSettings::Registry64Format, QSettings::Registry32Format}) {
        QSettings registry(QStringLiteral("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Audacious"),
                           format);
        const QString path = registry.value(QStringLiteral("InstallLocation")).toString();
        if (QDir::isAbsolutePath(path)) directories << path;
    }
    for (const char *key : {"ProgramW6432", "ProgramFiles", "ProgramFiles(x86)"}) {
        const QString path = qEnvironmentVariable(key);
        if (QDir::isAbsolutePath(path)) directories << QDir(path).filePath(QStringLiteral("Audacious"));
    }
    const QString user = qEnvironmentVariable("LOCALAPPDATA");
    if (QDir::isAbsolutePath(user)) directories << QDir(user).filePath(QStringLiteral("Programs/Audacious"));
    for (const QString &root : std::as_const(directories)) {
        for (const auto &relative : {QStringLiteral("bin/audacious.exe"), QStringLiteral("audacious.exe")}) {
            const QString filename = QDir(root).filePath(relative);
            if (QFileInfo(filename).isFile()) return QDir::cleanPath(filename);
        }
    }
    return {};
}

bool parsePoll(const QByteArray &output, int exitCode, AudaciousWindows::Status &status)
{
    const qsizetype newline = output.indexOf('\n');
    if (newline < 0) return false;
    const QByteArray state = output.left(newline).trimmed();
    if (state != "playing" && state != "paused" && state != "stopped") return false;
    const qsizetype titleStart = output.indexOf('\n', newline + 1);
    const auto volumeLine = titleStart < 0 ? output.mid(newline + 1)
                                          : output.mid(newline + 1, titleStart - newline - 1);
    bool validVolume = false;
    const int volume = volumeLine.trimmed().toInt(&validVolume);
    const bool volumeInRange = validVolume && volume >= 0 && volume <= 100;
    // audtool exits 1 if playback ends between its status and current-song commands.
    if (exitCode != 0 && !(exitCode == 1 && state == "stopped" && volumeInRange)) return false;
    status.connected = true;
    status.playing = state == "playing";
    status.hasTrack = state != "stopped";
    status.volume = volumeInRange ? volume : -1;
    status.title = titleStart < 0 || state == "stopped" ? QString()
        : QString::fromUtf8(output.mid(titleStart + 1)).simplified().left(512);
    return true;
}

} // namespace

AudaciousWindows::AudaciousWindows(int instance, QObject *parent)
    : QObject(parent), m_instance(qBound(1, instance, 9))
{
    m_timeout.setSingleShot(true);
    m_volumeDelay.setSingleShot(true);
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray incoming = m_process.readAllStandardOutput();
        if (incoming.size() > kMaxOutputBytes - m_output.size()) {
            m_invalidResponse = true;
            m_process.kill();
        } else {
            m_output += incoming;
        }
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        complete(exit == QProcess::NormalExit ? code : -1);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(-1);
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_invalidResponse = true;
        m_process.kill();
    });
    connect(&m_volumeDelay, &QTimer::timeout, this, &AudaciousWindows::runNext);
}

AudaciousWindows::~AudaciousWindows() { stop(); }

QStringList AudaciousWindows::instanceArgs() const
{
    return {QStringLiteral("-%1").arg(m_instance)};
}

void AudaciousWindows::tick()
{
    if (m_stopped || m_job != Job::None) return;
    const QString active = runningPlayer();
    if (!active.isEmpty()) m_player = active;
    else if (!QFileInfo(m_player).isFile()) m_player = installedPlayer();
    m_tool = m_player.isEmpty() ? QString() : QFileInfo(m_player).dir().filePath(QStringLiteral("audtool.exe"));
    m_status.installed = !m_player.isEmpty();
    m_status.running = !active.isEmpty();
    if (m_status.running && QFileInfo(m_tool).isFile()) {
        QStringList args{QStringLiteral("playback-status"), QStringLiteral("get-volume")};
        if (m_status.hasTrack) args << QStringLiteral("current-song");
        start(Job::Poll, args);
    } else {
        m_status.connected = false;
        m_status.playing = false;
        m_status.hasTrack = false;
        m_status.title.clear();
        m_status.volume = -1;
        m_commands.clear();
        m_pendingVolume = -1;
        emit changed();
    }
}

void AudaciousWindows::launch(const QString &arg)
{
    if (m_stopped || !QFileInfo(m_player).isFile()) return;
    QProcess player;
    auto environment = QProcessEnvironment::systemEnvironment();
    // Audacious ships its own Qt runtime and platform plugin.
    environment.remove(QStringLiteral("QT_PLUGIN_PATH"));
    environment.remove(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"));
    environment.remove(QStringLiteral("QT_QPA_PLATFORM"));
    player.setProcessEnvironment(environment);
    player.setProgram(m_player);
    player.setArguments(instanceArgs() + QStringList{arg});
    player.setWorkingDirectory(QFileInfo(m_player).absolutePath());
    player.startDetached();
}

void AudaciousWindows::open()
{
    launch(QStringLiteral("--show-main-window"));
}

void AudaciousWindows::control(const QString &method)
{
    if (m_stopped) return;
    if (!m_status.connected) {
        if (!m_status.running && method == QLatin1String("PlayPause")) launch(QStringLiteral("--play"));
        return;
    }
    QString command;
    if (method == QLatin1String("PlayPause")) command = QStringLiteral("playback-playpause");
    else if (method == QLatin1String("Previous")) command = QStringLiteral("playlist-reverse");
    else if (method == QLatin1String("Next")) command = QStringLiteral("playlist-advance");
    else return;
    if (m_commands.size() < kMaxCommands) m_commands.enqueue(command);
    runNext();
}

void AudaciousWindows::setVolume(int volume)
{
    if (m_stopped || !m_status.connected || m_status.volume < 0) return;
    m_pendingVolume = qBound(0, volume, 100);
    m_volumeDelay.start(120);
}

void AudaciousWindows::start(Job job, QStringList args)
{
    m_job = job;
    m_output.clear();
    m_invalidResponse = false;
    m_process.setWorkingDirectory(QFileInfo(m_tool).absolutePath());
    m_timeout.start(kProcessDeadlineMs);
    m_process.start(m_tool, instanceArgs() + args);
}

void AudaciousWindows::complete(int exitCode)
{
    const Job job = std::exchange(m_job, Job::None);
    if (m_stopped || job == Job::None) return;
    m_timeout.stop();
    const bool success = !m_invalidResponse
        && (job == Job::Poll ? parsePoll(m_output, exitCode, m_status) : exitCode == 0);
    if (!success) {
        m_status.connected = false;
        m_status.playing = false;
        m_status.hasTrack = false;
        m_status.title.clear();
        m_status.volume = -1;
        m_commands.clear();
        m_pendingVolume = -1;
    }
    emit changed();
    if (success) {
        // Start the next helper only after Qt finishes delivering the current process's exit signal.
        QTimer::singleShot(0, this, [this, job] {
            runNext();
            if (job == Job::Command && m_job == Job::None && !m_volumeDelay.isActive()) tick();
        });
    }
}

void AudaciousWindows::runNext()
{
    if (m_stopped || m_job != Job::None || !m_status.connected) return;
    if (!m_commands.isEmpty()) start(Job::Command, {m_commands.dequeue()});
    else if (m_pendingVolume >= 0 && !m_volumeDelay.isActive())
        start(Job::Command, {QStringLiteral("set-volume"), QString::number(std::exchange(m_pendingVolume, -1))});
}

void AudaciousWindows::stop()
{
    if (m_stopped) return;
    m_stopped = true;
    m_timeout.stop();
    m_volumeDelay.stop();
    m_commands.clear();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(500);
    }
}
