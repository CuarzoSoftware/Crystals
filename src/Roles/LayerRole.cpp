#include <Roles/LayerRole.h>
#include <Effects/TopbarAutoHide.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Core/Crystals.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>

SkIPoint LayerRole::rolePos() const
{
    // Moved, not just drawn elsewhere: input follows what's shown.
    // The desktop: away while a fullscreen workspace is shown (see Workspaces)
    // Top bars: up out of the way of Exposé's workspaces bar
    return LLayerRole::rolePos() - SkIPoint::Make(0, TopbarAutoHide::TopbarOffset(*this) + Expose::TopbarOffset(*this)) +
           Workspaces::ParkOffset(surface());
}

void LayerRole::stateChanged(CZBitset<Changes> changes, const State &prev) noexcept
{
    LLayerRole::stateChanged(changes, prev);

    // The scope may have changed (desktop, shell)
    if (auto *surf { static_cast<Surface*>(surface()) }; surf && !surf->parent())
    {
        surf->view.setParent(&GetScene()->containerFor(*surf));
        surf->view.syncOpaqueRegion(); // The desktop: always opaque
    }

    GetCompositor()->restack();
    TopbarAutoHide::UpdateAll(); // May have become (or stopped being) a top bar
}
