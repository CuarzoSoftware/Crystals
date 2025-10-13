#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>

void ToplevelRole::minimizedChanged()
{
    LToplevelRole::minimizedChanged();

    auto *surf { static_cast<Surface*>(surface()) };
    surf->view.syncVisibility();
}
