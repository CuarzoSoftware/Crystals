#include <Core/Volume.h>
#include <Core/Log.h>
#include <Effects/VolumeHUD.h>
#include <Seat/Output.h>
#include <Louvre/Cursor/LCursor.h>
#include <linux/input-event-codes.h>
#include <sys/eventfd.h>
#include <sys/wait.h>
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

extern char **environ;

static constexpr float Step { 1.f / 16.f };      // Like macOS: 16 steps
static constexpr UInt32 RepeatDelayMs { 450 };
static constexpr UInt32 RepeatIntervalMs { 90 };
static constexpr const char *FeedbackSound { "/usr/share/sounds/freedesktop/stereo/audio-volume-change.oga" };

/* ------------------------------------------ Helpers ------------------------------------------ */

// The environment with LC_ALL=C (untranslated output to parse).
static std::vector<char*> CEnv() noexcept
{
    std::vector<char*> env;

    for (char **e = environ; *e; e++)
        if (strncmp(*e, "LC_ALL=", 7) != 0)
            env.push_back(*e);

    env.push_back(const_cast<char*>("LC_ALL=C"));
    env.push_back(nullptr);
    return env;
}

/**
 * Runs a command and waits for it (background thread only). Returns its stdout if @p output,
 * or an empty string.
 */
static std::string Run(std::initializer_list<const char*> args, bool output = false) noexcept
{
    std::vector<char*> argv;

    for (const char *a : args)
        argv.push_back(const_cast<char*>(a));

    argv.push_back(nullptr);

    int fds[2] { -1, -1 };

    if (output && pipe2(fds, O_CLOEXEC) != 0)
        return {};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    if (output)
        posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    else
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);

    auto env { CEnv() };
    pid_t pid;
    const int ret { posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), env.data()) };
    posix_spawn_file_actions_destroy(&actions);

    std::string out;

    if (output)
    {
        close(fds[1]);

        if (ret == 0)
        {
            char buf[256];
            ssize_t n;

            while ((n = read(fds[0], buf, sizeof(buf))) > 0)
                out.append(buf, size_t(n));
        }

        close(fds[0]);
    }

    if (ret == 0)
        waitpid(pid, nullptr, 0);

    return out;
}

// Starts a command without waiting for it (reaped by a detached thread).
static void Start(std::initializer_list<const char*> args) noexcept
{
    std::vector<char*> argv;

    for (const char *a : args)
        argv.push_back(const_cast<char*>(a));

    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid;

    if (posix_spawnp(&pid, argv[0], &actions, nullptr, argv.data(), environ) == 0)
        std::thread([pid]{ waitpid(pid, nullptr, 0); }).detach();

    posix_spawn_file_actions_destroy(&actions);
}

/* ------------------------------------------ Volume ------------------------------------------ */

static std::unique_ptr<Volume> s_volume;

Volume &Volume::Get() noexcept
{
    if (!s_volume)
        s_volume.reset(new Volume());

    return *s_volume;
}

void Volume::Clear() noexcept
{
    s_volume.reset();
}

Volume::Volume() noexcept :
    m_hud(std::make_unique<VolumeHUD>())
{
    m_repeat.setCallback([this](CZTimer *timer){
        if (m_repeatKey == 0)
            return;

        run(m_repeatKey == KEY_VOLUMEUP ? Up : Down);
        timer->start(RepeatIntervalMs);
    });

    const int fd { eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK) };

    if (fd < 0)
    {
        Log(CZError, CZLN, "eventfd failed: volume keys disabled");
        return;
    }

    m_wake = CZEventSource::Make(fd, EPOLLIN, CZOwn::Own, [this](int fd, UInt32, CZEventSource*) {
        eventfd_t v;
        eventfd_read(fd, &v);

        std::vector<std::function<void()>> done;

        {
            std::lock_guard lock { m_mutex };
            done.swap(m_done);
        }

        for (auto &f : done)
            f();
    });

    m_thread = std::thread([this]{ loop(); });

    // The current state, so the first press shows the new level right away.
    enqueue([this]() -> std::function<void()> {
        const std::string out { Run({ "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@" }, true) };
        const bool dark { Run({ "gsettings", "get", "org.gnome.desktop.interface", "color-scheme" }, true).find("dark") != std::string::npos };
        float level;

        if (sscanf(out.c_str(), "Volume: %f", &level) != 1)
            return [this, dark]{ m_dark = dark; };

        const bool muted { out.find("[MUTED]") != std::string::npos };
        return [this, level, muted, dark]{
            // Unless a key already got the real state.
            if (m_level < 0.f)
            {
                m_level = std::min(level, 1.f);
                m_muted = muted;
            }

            m_dark = dark;
        };
    });
}

Volume::~Volume() noexcept
{
    {
        std::lock_guard lock { m_mutex };
        m_quit = true;
        m_jobs.clear();
    }

    m_cv.notify_one();

    if (m_thread.joinable())
        m_thread.join();
}

bool Volume::keyEvent(UInt32 keyCode, bool pressed) noexcept
{
    if (keyCode != KEY_VOLUMEUP && keyCode != KEY_VOLUMEDOWN && keyCode != KEY_MUTE)
        return false;

    if (!m_wake)
        return true; // Taken anyway

    if (!pressed)
    {
        if (keyCode == m_repeatKey)
        {
            m_repeatKey = 0;
            m_repeat.stop();
        }

        return true;
    }

    if (keyCode == KEY_MUTE)
    {
        run(Mute);
        return true;
    }

    run(keyCode == KEY_VOLUMEUP ? Up : Down);
    m_repeatKey = keyCode;
    m_repeat.start(RepeatDelayMs);
    return true;
}

void Volume::run(Action action) noexcept
{
    // Right away, the expected result (corrected once the real one is read back).
    if (m_level >= 0.f)
    {
        if (action == Mute)
            m_muted = !m_muted;
        else
        {
            m_level = std::clamp(std::round(m_level / Step) * Step + (action == Up ? Step : -Step), 0.f, 1.f);
            m_muted = false;
        }

        m_hud->show(m_level, m_muted, m_dark);
    }

    enqueue([this, action]() -> std::function<void()> {
        switch (action)
        {
        case Up:
            Run({ "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "0" });
            Run({ "wpctl", "set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@", "6.25%+" });
            break;
        case Down:
            Run({ "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "0" });
            Run({ "wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "6.25%-" });
            break;
        case Mute:
            Run({ "wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle" });
            break;
        }

        // "Volume: 0.45" or "Volume: 0.45 [MUTED]"
        const std::string out { Run({ "wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@" }, true) };
        float level { -1.f };
        bool muted { false };

        if (sscanf(out.c_str(), "Volume: %f", &level) == 1)
            muted = out.find("[MUTED]") != std::string::npos;
        else
            level = -1.f;

        // The HUD follows the desktop's light/dark preference.
        const bool dark { Run({ "gsettings", "get", "org.gnome.desktop.interface", "color-scheme" }, true).find("dark") != std::string::npos };

        // How loud it is now (not when silent). At most a few per second while holding the key.
        if (action != Mute && level > 0.f && !muted && access(FeedbackSound, R_OK) == 0)
        {
            static auto lastSound { std::chrono::steady_clock::time_point() };
            const auto now { std::chrono::steady_clock::now() };

            if (now - lastSound > std::chrono::milliseconds(150))
            {
                lastSound = now;
                Start({ "pw-play", FeedbackSound });
            }
        }

        return [this, level, muted, dark]{
            if (level < 0.f)
            {
                Log(CZWarning, CZLN, "Couldn't read the volume (wpctl)");
                return;
            }

            m_level = std::min(level, 1.f);
            m_muted = muted;
            m_dark = dark;
            m_hud->show(m_level, m_muted, m_dark);
        };
    });
}

void Volume::enqueue(Job job) noexcept
{
    {
        std::lock_guard lock { m_mutex };
        m_jobs.emplace_back(std::move(job));
    }

    m_cv.notify_one();
}

void Volume::loop() noexcept
{
    while (true)
    {
        Job job;

        {
            std::unique_lock lock { m_mutex };
            m_cv.wait(lock, [this]{ return m_quit || !m_jobs.empty(); });

            if (m_quit)
                return;

            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        auto done { job() };

        if (!done)
            continue;

        {
            std::lock_guard lock { m_mutex };

            if (m_quit)
                return;

            m_done.emplace_back(std::move(done));
        }

        eventfd_write(m_wake->fd(), 1);
    }
}
