#include <Roles/LayerRole.h>
#include <Effects/TopbarAutoHide.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <Core/Crystals.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>
#include <Louvre/Cursor/LCursor.h>
#include <Effects/SurfaceBlurManager.h>

SkIPoint LayerRole::rolePos() const
{
    // Moved, not just drawn elsewhere: input follows what's shown.
    // The desktop: away while a fullscreen workspace is shown (see Workspaces)
    // Top bars: above the screen once faded out by Exposé (no input under its workspaces bar)
    return LLayerRole::rolePos() - SkIPoint::Make(0, TopbarAutoHide::TopbarOffset(*this) + Expose::TopbarOffset(*this)) +
           Workspaces::ParkOffset(surface());
}

void LayerRole::configureRequest()
{
    // As LLayerRole's default (keep in sync), only remembering the size sent
    if (!exclusiveOutput())
        setExclusiveOutput(cursor()->output());

    surface()->sendOutputEnterEvent(exclusiveOutput());

    SkISize newSize { size() };
    const CZBitset<CZEdge> edge { exclusiveZone().edge() };

    if (newSize.width() == 0 || edge == 0 || anchor().hasAll(CZEdgeLeft | CZEdgeRight))
        newSize.fWidth = exclusiveZone().rect().width();

    if (newSize.height() == 0 || edge == 0 || anchor().hasAll(CZEdgeTop | CZEdgeBottom))
        newSize.fHeight = exclusiveZone().rect().height();

    m_configuredSize = newSize;
    configureSize(newSize);
}

bool LayerRole::settled() const noexcept
{
    return m_configuredSize.isEmpty() || surface()->size() == m_configuredSize;
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

    // The edge it slides in through may have changed (see SurfaceBlurManager::updateCaptureInside())
    if (auto *blur { surface() ? static_cast<SurfaceBlurManager*>(surface()->backgroundBlur()) : nullptr })
        blur->updateCaptureInside();

    GetCompositor()->restack();
    TopbarAutoHide::UpdateAll(); // May have become (or stopped being) a top bar
}
