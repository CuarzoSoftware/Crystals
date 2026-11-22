#ifndef OUTPUTCONFIG_H
#define OUTPUTCONFIG_H

#include <Core/Types.h>
#include <filesystem>
#include <vector>

/**
 * @brief The displays' configuration, saved to a file and restored when they're connected again.
 *
 * Saved to FilePath() (`~/.config/cz-crystals/outputs.conf`) SaveDelayMs after a client applies a
 * configuration (see Seat::configureOutputsRequest()), not right away: a client that applied it
 * may still revert it if the user doesn't confirm it (e.g. System Settings' Displays). Saved:
 *
 * - Per setup (the set of displays connected at once, identified by their make, model and serial
 *   number): each display's mode, position, transform, scale and whether it's enabled, along with
 *   everything known about it (connector, description, physical size, DPI, modes...).
 * - Per display: its last configuration, for setups not saved yet.
 *
 * Restored at startup (InitializeOutputs(), instead of Louvre's left-to-right arrangement) and
 * when displays are plugged or unplugged (Restore()): the saved setup of the connected displays,
 * or else each display's last mode, transform and scale, arranged left to right.
 */
class OutputConfig
{
public:
    static constexpr UInt32 SaveDelayMs { 25000 };

    static std::filesystem::path FilePath() noexcept;

    /// Initializes the outputs at startup (see LCompositor::initialized()) as saved.
    static void InitializeOutputs() noexcept;

    /**
     * @brief Applies the saved setup of the connected outputs (but @p gone, being unplugged), if any.
     *
     * @return `false` if there's none (nothing changed).
     */
    static bool Restore(LOutput *gone = nullptr) noexcept;

    /// @p output was plugged: configured as saved (see Restore()), or with its last configuration at the right end.
    static void OutputPlugged(LOutput *output) noexcept;

    /// A configuration was applied: saved in SaveDelayMs (restarted if another one comes meanwhile).
    static void ScheduleSave() noexcept;

    /// Saves the current configuration now.
    static void Save() noexcept;
};

#endif // OUTPUTCONFIG_H
