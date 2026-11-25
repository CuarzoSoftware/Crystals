#ifndef OPENFROMICON_H
#define OPENFROMICON_H

#include <Core/Types.h>
#include <string>

/**
 * @brief Windows Desk opens from a desktop icon grow out of that icon.
 *
 * Right before mapping such a window, Desk calls OpenFromIcon on the session bus (see ShellBus)
 * with its title and the icon's rect, and waits for the reply: when the window maps, Crystals
 * already knows. The first Desk window mapped with that title then grows from the icon like when
 * it's unminimized (see MinimizeAnimation::StartFrom()). Every other window is shown right away.
 */
class OpenFromIcon
{
public:
    /// How long an announced window is waited for.
    static constexpr UInt32 ExpectMs { 3000 };

    /// Desk will map a window titled @p title opened from the icon at @p icon (global coordinates).
    static void Expect(const std::string &title, const SkRect &icon) noexcept;

    /// A toplevel was mapped for the first time: grows from its icon if Desk announced it.
    static void WindowMapped(LToplevelRole &toplevel) noexcept;

    static void Clear() noexcept;
};

#endif // OPENFROMICON_H
