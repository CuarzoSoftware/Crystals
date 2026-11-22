#ifndef ATTENTION_H
#define ATTENTION_H

#include <Core/Types.h>
#include <Louvre/Manager/LActivationTokenManager.h>
#include <memory>

class ToplevelRole;

/**
 * @brief Windows that want the user's attention without taking them away from what they're doing.
 *
 * While the user is in a fullscreen window's workspace, or on a desktop other than the window's, a
 * window that appears or asks to be activated elsewhere doesn't switch workspaces (nor get
 * activated): its app's dock icon bounces and the alert sound plays instead (like macOS). It's
 * activated, and its workspace shown, when the user goes to it (e.g. clicking the dock icon).
 *
 * That's for:
 * - New windows landing in another workspace (new windows go to the first desktop while a
 *   fullscreen window's workspace is shown), see ToplevelRole::configureRequest().
 * - Activations asked by apps (xdg-activation, e.g. a link opened in a browser of another
 *   workspace), see ActivationTokenManager.
 *
 * Not for what the user asks for: clicking an app in the dock (activating its window, or launching
 * it: Shelf tells, see UserLaunched()), Exposé, new windows of the app being used (e.g. a new
 * browser window opened from a fullscreen one), dialogs (child windows go with their parent).
 *
 * The dock is told through the session bus (see ShellBus): Shelf bounces that app's icon until one
 * of its windows is activated, without showing the dock.
 */
class Attention
{
public:
    /// Whether activating @p toplevel now would take the user to another workspace (see above).
    static bool WouldLeave(const ToplevelRole &toplevel) noexcept;

    /**
     * @brief Asks for the user's attention for @p toplevel: the alert sound, and its dock icon
     *        bounces. Not mapped yet: once mapped (its app id is known then, see Surface::mappingChanged()).
     */
    static void Request(ToplevelRole &toplevel) noexcept;

    /// Whether ActivationTokenManager is activating a surface (an app asked for it).
    static bool ActivatingByToken() noexcept;

    /// The user launched @p appId from the dock (see ShellBus): its first window is shown, wherever
    /// it opens (or activated by the app with a token), within UserLaunchTimeoutMs.
    static void UserLaunched(const std::string &appId) noexcept;
    static constexpr UInt32 UserLaunchTimeoutMs { 30000 };

    /// Whether a window of @p appId is expected because the user launched it (see UserLaunched()).
    static bool IsUserLaunch(const std::string &appId) noexcept;

    /// Like IsUserLaunch(), and forgets it (its window showed up).
    static bool ConsumeUserLaunch(const std::string &appId) noexcept;
};

/// Marks the activations asked by apps (xdg-activation tokens), see Attention.
class ActivationTokenManager final : public LActivationTokenManager
{
public:
    using LActivationTokenManager::LActivationTokenManager;
    void activateSurfaceRequest(LSurface *surface) override;
};

#endif // ATTENTION_H
