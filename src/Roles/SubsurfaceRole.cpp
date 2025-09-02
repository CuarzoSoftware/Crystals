#include <Roles/SubsurfaceRole.h>
#include <Roles/Surface.h>

SubsurfaceRole::SubsurfaceRole(const void *params) noexcept : LSubsurfaceRole(params) {}

void SubsurfaceRole::localPosChanged() noexcept
{
    auto *surf { static_cast<Surface*>(surface()) };
    surf->view.layout().setPosition(YGEdgeLeft, localPos().x());
    surf->view.layout().setPosition(YGEdgeTop, localPos().y());
}

void SubsurfaceRole::placedAbove(LSurface *sibling) noexcept
{
    auto *surf { static_cast<Surface*>(surface()) };
    auto *sibl { static_cast<Surface*>(sibling) };

    // Sibling is the parent => insert it at the begining of parent->view.above
    if (sibl == surf->parent())
    {
        if (sibl->view.above.children().empty())
            surf->view.setParent(&sibl->view.above);
        else
            surf->view.insertBefore(sibl->view.above.children().front());
    }
    else
        surf->view.insertAfter(&sibl->view);
}

void SubsurfaceRole::placedBelow(LSurface *sibling) noexcept
{
    auto *surf { static_cast<Surface*>(surface()) };
    auto *sibl { static_cast<Surface*>(sibling) };

    // Sibling is the parent => insert it at the end of parent->view.below
    if (sibl == surf->parent())
        surf->view.setParent(&sibl->view.above);
    else
        surf->view.insertAfter(&sibl->view);
}
