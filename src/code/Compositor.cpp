#include <LTime.h>
#include <LSeat.h>
#include <LKeyboard.h>
#include <LOutput.h>
#include <LOutputMode.h>

#include "Compositor.h"
#include "Output.h"
#include "Surface.h"
#include "ToplevelRole.h"
#include "Popup.h"
#include "Pointer.h"

#include "View.h"

Compositor::Compositor():LCompositor()
{
    mainView = new View();
    backgroundLayer = new View(mainView);
    surfacesLayer = new View(mainView);
    overlayLayer = new View(mainView);
}

LOutput *Compositor::createOutputRequest()
{
    return new Output();
}

LSurface *Compositor::createSurfaceRequest(LSurface::Params *params)
{
    Surface *newSurface = new Surface(params);
    return newSurface;
}

void Compositor::destroySurfaceRequest(LSurface *s)
{
    for (Output *output : (std::list<Output*>&)outputs())
        if (s == output->fullscreenSurface)
            output->fullscreenSurface = nullptr;
}

LToplevelRole *Compositor::createToplevelRoleRequest(LToplevelRole::Params *params)
{
    return new ToplevelRole(params);
}

LPopupRole *Compositor::createPopupRoleRequest(LPopupRole::Params *params)
{
    return new Popup(params);
}

LPointer *Compositor::createPointerRequest(LPointer::Params *params)
{
    return new Pointer(params);
}
