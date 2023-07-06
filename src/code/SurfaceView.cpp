#include <LCursor.h>
#include "Compositor.h"
#include "SurfaceView.h"
#include "Surface.h"
#include "Output.h"

SurfaceView::SurfaceView(Surface *surface) : View(((Compositor*)compositor())->surfacesLayer)
{
    this->surface = surface;
}

VIEW_RENDER_MODE SurfaceView::renderMode() const
{
    return VIEW_RENDER_MODE_ITSELF_ONLY;
}

void SurfaceView::setPosC(const LPoint &pos)
{
    surface->setPosC(pos);
}

const LPoint *SurfaceView::posC() const
{
    return &surface->rolePosC();
}

const LPoint *SurfaceView::sizeC() const
{
    return &surface->sizeC();
}

const LTexture *SurfaceView::texture() const
{
    return surface->texture();
}

bool SurfaceView::mapped() const
{
    return surface->mapped() && !surface->minimized() && !surface->cursorRole();
}

Int32 SurfaceView::scale() const
{
    return surface->bufferScale();
}

UInt32 SurfaceView::damageId() const
{
    return surface->damageId();
}

const LRegion *SurfaceView::inputRegionC() const
{
    return &surface->inputRegionC();
}

const LRegion *SurfaceView::opaqueRegionC() const
{
    return &surface->opaqueRegionC();
}

const LRegion *SurfaceView::translucentRegionC() const
{
    return &surface->translucentRegionC();
}

const LRegion *SurfaceView::damageC() const
{
    return &surface->damagesC();
}

void SurfaceView::paintBegin(Output *output)
{
    L_UNUSED(output);

    // Request next frame on cursor surfaces so that
    // client can correctly update cursor and scale
    if (surface->cursorRole())
    {
        for (LOutput *o : compositor()->outputs())
        {
            if (o->rectC().intersects(cursor()->rectC()))
                surface->sendOutputEnterEvent(o);
            else
                surface->sendOutputLeaveEvent(o);
        }

        surface->requestNextFrame(false);
    }
}

void SurfaceView::enterOutput(Output *output)
{
    surface->sendOutputEnterEvent(output);
}

void SurfaceView::leaveOutput(Output *output)
{
    surface->sendOutputLeaveEvent(output);
}

void SurfaceView::requestNextFrame(bool clearDamage)
{
    surface->requestNextFrame(clearDamage);
}
