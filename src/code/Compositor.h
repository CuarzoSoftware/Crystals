#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <LCompositor.h>
#include <LXCursor.h>

class View;

using namespace Louvre;

class Compositor : public LCompositor
{
public:
    Compositor();

    LOutput *createOutputRequest() override;
    LSurface *createSurfaceRequest(LSurface::Params *params) override;
    LToplevelRole *createToplevelRoleRequest(LToplevelRole::Params *params) override;
    LPopupRole *createPopupRoleRequest(LPopupRole::Params *params) override;
    LPointer *createPointerRequest(LPointer::Params *params) override;

    void destroySurfaceRequest(LSurface *surface) override;


    View *mainView;
    View *backgroundLayer, *surfacesLayer, *overlayLayer;
};

#endif // COMPOSITOR_H
