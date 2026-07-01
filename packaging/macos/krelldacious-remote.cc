#include <libaudcore/drct.h>
#include <libaudcore/plugin.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

std::string socketPath()
{
    return std::string("/tmp/krelldacious-audacious-") + std::to_string(getuid()) + ".sock";
}

std::string statusText()
{
    return std::string("playing=") + (aud_drct_get_playing() ? "1" : "0")
        + " paused=" + (aud_drct_get_paused() ? "1" : "0")
        + " ready=" + (aud_drct_get_ready() ? "1" : "0")
        + " position=" + std::to_string(aud_drct_get_position()) + "\n";
}

std::string runCommand(std::string cmd)
{
    while (!cmd.empty() && (cmd.back() == 10 || cmd.back() == 13 || cmd.back() == 32 || cmd.back() == 9))
        cmd.pop_back();
    if (cmd == "play")
        aud_drct_play();
    else if (cmd == "pause")
        aud_drct_pause();
    else if (cmd == "playpause")
        aud_drct_play_pause();
    else if (cmd == "next")
        aud_drct_pl_next();
    else if (cmd == "previous")
        aud_drct_pl_prev();
    else if (cmd == "stop")
        aud_drct_stop();
    else if (cmd != "status")
        return "error=unknown-command\n";
    return statusText();
}

class KrelldaciousRemotePlugin : public GeneralPlugin
{
public:
    static constexpr PluginInfo info = {
        "Krelldacious Remote",
        "audacious-plugins",
        "Local control bridge for the Krellix Krelldacious monitor.",
        nullptr,
        0,
    };

    KrelldaciousRemotePlugin() : GeneralPlugin(info, true) {}

    bool init() override
    {
        m_path = socketPath();
        unlink(m_path.c_str());
        m_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (m_fd < 0)
            return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", m_path.c_str());
        if (bind(m_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 || listen(m_fd, 8) != 0) {
            close(m_fd);
            m_fd = -1;
            unlink(m_path.c_str());
            return false;
        }

        m_running = true;
        m_thread = std::thread([this]() { serve(); });
        return true;
    }

    void cleanup() override
    {
        m_running = false;
        if (m_fd >= 0) {
            shutdown(m_fd, SHUT_RDWR);
            close(m_fd);
            m_fd = -1;
        }
        if (m_thread.joinable())
            m_thread.join();
        if (!m_path.empty())
            unlink(m_path.c_str());
    }

private:
    void serve()
    {
        while (m_running) {
            const int client = accept(m_fd, nullptr, nullptr);
            if (client < 0) {
                if (m_running)
                    usleep(10000);
                continue;
            }
            char buf[128]{};
            const ssize_t n = read(client, buf, sizeof(buf) - 1);
            std::string out = n > 0 ? runCommand(std::string(buf, static_cast<size_t>(n))) : "error=empty\n";
            write(client, out.data(), out.size());
            close(client);
        }
    }

    std::atomic<bool> m_running{false};
    int m_fd = -1;
    std::string m_path;
    std::thread m_thread;
};

} // namespace

extern "C" __attribute__((visibility("default"))) KrelldaciousRemotePlugin aud_plugin_instance;
KrelldaciousRemotePlugin aud_plugin_instance;
