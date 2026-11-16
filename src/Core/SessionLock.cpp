#include <Core/SessionLock.h>
#include <Core/Log.h>
#include <Scene/Scene.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Seat/LPointer.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Roles/LSessionLockRole.h>
#include <Louvre/Roles/LSubsurfaceRole.h>
#include <Louvre/Roles/LToplevelRole.h>

void SessionLockManager::stateChanged()
{
    // Focus on the cursor's output lock surface (or none), drags cancelled...
    LSessionLockManager::stateChanged();

    const bool locked { state() != Unlocked };
    Log(CZInfo, CZLN, "Session {}", state() == LSessionLockManager::Locked ? "locked" : state() == DeadLocked ? "dead locked (the lock client died)" : "unlocked");

    if (Scene *scene { GetScene() })
        scene->setLocked(locked);

    if (!locked)
    {
        // Back to the active window
        seat()->pointer()->setFocus(nullptr);
        seat()->keyboard()->setFocus(seat()->activeToplevel() ? seat()->activeToplevel()->surface() : nullptr);
    }

    // The locked frame (Louvre sends "locked" once every output presented it)
    for (LOutput *output : compositor()->outputs())
        output->repaint();
}

bool SessionLockManager::IsLocked() noexcept
{
    return sessionLockManager() && sessionLockManager()->state() != Unlocked;
}

bool SessionLockManager::IsLockClientSurface(const LSurface *surface) noexcept
{
    return surface && sessionLockManager() && sessionLockManager()->client() &&
           surface->client() == sessionLockManager()->client();
}

static LSurface *SubsurfacesAt(const std::vector<LSubsurfaceRole*> &list, SkIPoint p) noexcept
{
    for (auto it = list.rbegin(); it != list.rend(); it++)
    {
        LSurface *s { (*it)->surface() };

        if (!s->mapped())
            continue;

        if (LSurface *hit { SubsurfacesAt(s->subsurfacesAbove(), p) })
            return hit;

        if (s->inputRegionContainsGlobalPoint(p))
            return s;

        if (LSurface *hit { SubsurfacesAt(s->subsurfacesBelow(), p) })
            return hit;
    }

    return nullptr;
}

LSurface *SessionLockManager::SurfaceAt(SkIPoint pos) noexcept
{
    for (LOutput *output : compositor()->outputs())
    {
        LSessionLockRole *role { output->sessionLockRole() };

        if (!role || !role->surface() || !role->surface()->mapped() || !output->rect().contains(pos.x(), pos.y()))
            continue;

        LSurface *s { role->surface() };

        if (LSurface *hit { SubsurfacesAt(s->subsurfacesAbove(), pos) })
            return hit;

        if (s->inputRegionContainsGlobalPoint(pos))
            return s;

        return SubsurfacesAt(s->subsurfacesBelow(), pos);
    }

    return nullptr;
}

LSurface *SessionLockManager::FocusSurface() noexcept
{
    if (!IsLocked())
        return nullptr;

    if (LOutput *output { cursor()->output() }; output && output->sessionLockRole() && output->sessionLockRole()->surface() &&
        output->sessionLockRole()->surface()->mapped())
        return output->sessionLockRole()->surface();

    for (LOutput *output : compositor()->outputs())
        if (output->sessionLockRole() && output->sessionLockRole()->surface() && output->sessionLockRole()->surface()->mapped())
            return output->sessionLockRole()->surface();

    return nullptr;
}
