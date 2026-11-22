#ifndef SOUNDSETTINGS_H
#define SOUNDSETTINGS_H

#include <Core/Types.h>
#include <filesystem>
#include <string>

/**
 * @brief The alert and sound effects settings, chosen in System Settings (Sound).
 *
 * Stored in `$XDG_CONFIG_HOME/cz-crystals/sound.conf` (`~/.config/cz-crystals/sound.conf` by
 * default) by System Settings (see its AlertSettings, keep both in sync). Read again each time
 * (Load()): it's tiny, and changes apply right away.
 *
 * Sounds are played with `pw-play`, without waiting for them.
 */
struct SoundSettings
{
    std::string sound { "bell" };   ///< Alert sound: a freedesktop sound theme name (or a path)
    std::string device;             ///< Output (PipeWire node name) for alerts, empty: the default one
    float volume { 0.75f };         ///< Alert volume, 0..1
    bool startupSound { false };    ///< Played when Crystals starts
    bool interfaceSounds { true };  ///< Crystals' own sound effects (see PlayInterfaceSound())
    bool volumeFeedback { true };   ///< Played when the volume keys change the volume (see Volume)

    static std::filesystem::path FilePath() noexcept;

    /// The current settings (defaults if never saved). Thread safe.
    static SoundSettings Load() noexcept;

    /// The file of a sound theme name (or @p name if it's a path), empty if not found.
    static std::string SoundPath(const std::string &name) noexcept;

    /// Plays @p path through @p device (empty: the default output) at @p volume (0..1). Thread safe.
    static void Play(const std::string &path, const std::string &device = "", float volume = 1.f) noexcept;

    /// The alert sound, as set (e.g. a client rang the system bell). At most a few per second.
    static void PlayAlert() noexcept;

    /// The startup sound, if enabled.
    static void PlayStartup() noexcept;

    /// One of Crystals' own sound effects (a sound theme name), if enabled. Played like alerts.
    static void PlayInterfaceSound(const std::string &name) noexcept;
};

#endif // SOUNDSETTINGS_H
