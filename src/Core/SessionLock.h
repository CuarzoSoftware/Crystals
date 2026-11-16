#ifndef SESSIONLOCK_H
#define SESSIONLOCK_H

#include <Core/Types.h>
#include <Louvre/Manager/LSessionLockManager.h>
#include <CZ/skia/core/SkPoint.h>

/**
 * @brief The session lock (ext-session-lock-v1, e.g. cz-doorman-lock).
 *
 * Louvre handles the protocol (and filters the input of other clients in its default handlers).
 * While locked, Crystals shows nothing but the lock surfaces (over black: every other container of
 * the Scene is hidden), only they get input and the keyboard focus, and its own shortcuts and
 * gestures (Exposé, workspaces, decorations) are disabled. If the locking client dies the session
 * stays locked (black): another client may lock it again and unlock it.
 *
 * Once unlocked, the desktop fades in from black (see Scene::setLocked()).
 */
class SessionLockManager final : public LSessionLockManager
{
public:
    using LSessionLockManager::LSessionLockManager;

    void stateChanged() override;

    /// Whether the session is locked (also if the locking client died).
    static bool IsLocked() noexcept;

    /// Whether @p surface is (part of) a surface of the locking client.
    static bool IsLockClientSurface(const LSurface *surface) noexcept;

    /// The lock surface (or one of its subsurfaces) at @p pos, if any.
    static LSurface *SurfaceAt(SkIPoint pos) noexcept;

    /// The lock surface that should have the keyboard focus: the cursor's output one, else any.
    static LSurface *FocusSurface() noexcept;
};

#endif // SESSIONLOCK_H
