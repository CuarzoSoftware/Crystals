#ifndef SHELLBUS_H
#define SHELLBUS_H

#include <Core/Types.h>
#include <string>

/**
 * @brief Crystals ↔ Shelf (the dock), through the session bus.
 *
 * Crystals emits, on /org/cuarzo/Crystals (interface org.cuarzo.Crystals):
 * - AppAttention(s app id): the app's dock icon bounces (see Attention). The dock isn't shown for it.
 * - FullscreenChanged(b fullscreen, s output name): whether that output shows a fullscreen window's
 *   workspace (its dock auto-hides there, even if auto-hide is disabled).
 *
 * Shelf emits, on /org/cuarzo/Shelf (interface org.cuarzo.Shelf):
 * - AppLaunched(s app id): the user launched it from the dock: its first window is shown wherever
 *   it opens (see Attention::UserLaunched()).
 * - Started(): Shelf (re)started: the current FullscreenChanged state is sent again.
 */
class ShellBus
{
public:
    /// Connects and listens to Shelf (see Crystals' initialization).
    static void Init() noexcept;
    static void Clear() noexcept;

    static void EmitAppAttention(const std::string &appId) noexcept;
    static void EmitFullscreenChanged(bool fullscreen, const std::string &output) noexcept;
};

#endif // SHELLBUS_H
