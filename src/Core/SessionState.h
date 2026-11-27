#ifndef SESSIONSTATE_H
#define SESSIONSTATE_H

#include <Core/Types.h>

/**
 * @brief Where everything is while the user is in another session (TTY), put back when they return.
 *
 * Switching to another TTY disables the seat, and the DRM backend then removes every output (and
 * plugs the connected ones again, as new outputs, once the session is back: see
 * LSeat::enabledChanged()). Handled like real unplugs, each output's windows would pile up on the
 * next one, then on none, leave fullscreen, and lose their workspaces.
 *
 * Instead, Save() (the seat is being disabled, the outputs are still there) remembers:
 * - Each output (identified by its make, model and serial number, then its connector): position,
 *   mode, transform, scale, and its workspaces (the desktops and the fullscreen windows' ones, in
 *   order, and the one shown).
 * - Each window: its output, where it is in it, its desktop, and (fullscreen) where it goes back to.
 * - The cursor's position.
 *
 * Everything animated ends first, where it was going (Exposé, show desktop, workspace slides and
 * swipes, windows being minimized or entering/leaving fullscreen...): they're drawn on, and refer
 * to, the outputs about to be destroyed.
 *
 * While Suspended(), outputs go away without their windows being moved (see Seat::outputUnplugged()).
 *
 * Restore() (the seat is enabled again, the connected outputs are back) then:
 * - Arranges the outputs as they were, if they're all there (and no other one is).
 * - Gives each output that's still there its workspaces, and its windows their place in it. Windows
 *   closed meanwhile are just skipped (so are the workspaces of fullscreen ones).
 * - Puts the windows of outputs disconnected meanwhile on the closest remaining one, like when an
 *   output is unplugged (see Workspaces::Place()).
 *
 * The outputs stay black meanwhile, until their desktop shell is back on them (see
 * ResizeBlackout::BeginAll()) or BlackoutTimeoutMs passed.
 *
 * Back without any output connected: restored once one is plugged (see OutputPlugged()).
 */
class SessionState
{
public:
    /// Longest the outputs stay black once back, waiting for the desktop, the docks and the top bars.
    static constexpr UInt32 BlackoutTimeoutMs { 3000 };

    /// The seat is being disabled (before the outputs are removed).
    static void Save() noexcept;

    /// The seat is enabled again (after the connected outputs were plugged).
    static void Restore() noexcept;

    /// Between Save() and Restore(): outputs going away aren't real unplugs, and none paints (see
    /// Output::paintGL()).
    static bool Suspended() noexcept;

    /// An output was plugged: restores what Restore() couldn't without outputs, if anything.
    static void OutputPlugged() noexcept;
};

#endif // SESSIONSTATE_H
