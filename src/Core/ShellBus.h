#ifndef SHELLBUS_H
#define SHELLBUS_H

#include <Core/Types.h>
#include <string>

/**
 * @brief Crystals ↔ Shelf (the dock) and Desk, through the session bus.
 *
 * Crystals owns org.cuarzo.Crystals, with a method on /org/cuarzo/Crystals (interface
 * org.cuarzo.Crystals), only for Desk (the caller's process must be Desk's):
 * - OpenFromIcon(s title, i x, i y, i width, i height): Desk is about to map a window with that title,
 *   opened from the desktop icon at that rect (global coordinates). It grows from there (see
 *   OpenFromIcon). Desk waits for the reply before mapping it.
 *
 * Crystals emits, on /org/cuarzo/Crystals (interface org.cuarzo.Crystals):
 * - AppAttention(s app id): the app's dock icon bounces (see Attention). The dock isn't shown for it.
 * - FullscreenChanged(b fullscreen, s output name): whether that output shows a fullscreen window's
 *   workspace (its dock auto-hides there, even if auto-hide is disabled).
 * - WorkspaceChanged(s output name, u index): that output switched to another workspace (e.g.
 *   Gazette hides its notification center).
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
    static void EmitWorkspaceChanged(const std::string &output, UInt32 index) noexcept;
};

#endif // SHELLBUS_H
