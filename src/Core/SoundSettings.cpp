#include <Core/SoundSettings.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static constexpr const char *SoundThemeDir { "/usr/share/sounds/freedesktop/stereo/" };
static constexpr const char *StartupSound { "service-login" };

static std::string Trim(const std::string &s) noexcept
{
    const auto b { s.find_first_not_of(" \t\r\n") };

    if (b == std::string::npos)
        return {};

    return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

std::filesystem::path SoundSettings::FilePath() noexcept
{
    std::filesystem::path dir;

    if (const char *config { getenv("XDG_CONFIG_HOME") }; config && config[0] == '/')
        dir = config;
    else if (const char *home { getenv("HOME") })
        dir = std::filesystem::path(home) / ".config";
    else
        return {};

    return dir / "cz-crystals" / "sound.conf";
}

SoundSettings SoundSettings::Load() noexcept
{
    SoundSettings s;
    std::ifstream file { FilePath() };
    std::string line;

    const auto boolean = [](const std::string &v) { return v == "true" || v == "1" || v == "yes"; };

    while (std::getline(file, line))
    {
        const auto eq { line.find('=') };

        if (line.empty() || line[0] == '#' || line[0] == '[' || eq == std::string::npos)
            continue;

        const std::string key { Trim(line.substr(0, eq)) }, value { Trim(line.substr(eq + 1)) };

        if (key == "AlertSound")
            s.sound = value;
        else if (key == "AlertDevice")
            s.device = value;
        else if (key == "AlertVolume")
            s.volume = std::clamp(float(std::atof(value.c_str())), 0.f, 1.f);
        else if (key == "StartupSound")
            s.startupSound = boolean(value);
        else if (key == "InterfaceSounds")
            s.interfaceSounds = boolean(value);
        else if (key == "VolumeFeedback")
            s.volumeFeedback = boolean(value);
    }

    return s;
}

std::string SoundSettings::SoundPath(const std::string &name) noexcept
{
    if (name.empty())
        return {};

    const std::string path { name[0] == '/' ? name : std::string(SoundThemeDir) + name + ".oga" };
    return access(path.c_str(), R_OK) == 0 ? path : std::string();
}

void SoundSettings::Play(const std::string &path, const std::string &device, float volume) noexcept
{
    if (path.empty() || volume <= 0.f)
        return;

    char volumeStr[16];
    snprintf(volumeStr, sizeof(volumeStr), "%.2f", double(std::clamp(volume, 0.f, 1.f)));
    std::vector<std::string> args { "pw-play", "--volume", volumeStr };

    if (!device.empty())
    {
        args.push_back("--target");
        args.push_back(device);
    }

    args.push_back(path);

    std::vector<char*> argv;

    for (auto &a : args)
        argv.push_back(a.data());

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

void SoundSettings::PlayAlert() noexcept
{
    // Clients may ring it many times in a row (e.g. a key held in an empty text field)
    static std::mutex mutex;
    static auto last { std::chrono::steady_clock::time_point() };

    {
        std::lock_guard lock { mutex };
        const auto now { std::chrono::steady_clock::now() };

        if (now - last < std::chrono::milliseconds(150))
            return;

        last = now;
    }

    const SoundSettings s { Load() };
    Play(SoundPath(s.sound), s.device, s.volume);
}

void SoundSettings::PlayStartup() noexcept
{
    const SoundSettings s { Load() };

    if (s.startupSound)
        Play(SoundPath(StartupSound), s.device, s.volume);
}

void SoundSettings::PlayInterfaceSound(const std::string &name) noexcept
{
    const SoundSettings s { Load() };

    if (s.interfaceSounds)
        Play(SoundPath(name), s.device, s.volume);
}
