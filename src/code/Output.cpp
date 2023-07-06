#include <LPainter.h>
#include <LToplevelRole.h>
#include <LTime.h>
#include <LCursor.h>
#include <LLog.h>
#include <LOpenGL.h>

#include "View.h"
#include "Surface.h"
#include "Output.h"
#include "Compositor.h"

Output::Output():LOutput()
{

}

void Output::fullDamage()
{
    topbarHeight = 32 * compositor()->globalScale();

    if (buffersCount() > 1)
    {
        damage.clear();
        damage.addRect(rectC());
    }

    newDamage.clear();
    newDamage.addRect(rectC());
}

static void hideChildrenForThisFrame(View *view)
{
    for (View *child : view->children())
    {
        child->isRenderable = false;
        hideChildrenForThisFrame(child);
    }
}

void Output::calculateNewDamage(View *view, bool force)
{
    // Notify view paint began
    view->paintBegin(this);

    // Quick output data handle
    view->currentOutputData = &view->outputsMap[this];

    // Cache mapped call
    view->isRenderable = view->mapped();

    // If not mapped (do not paint and children too)
    if (!view->isRenderable)
    {
        hideChildrenForThisFrame(view);
        return;
    }

    // Calculate damage
    if (force)
    {
        // If the scale is equal to the global scale, we avoid performing transformations later
        view->bufferScaleMatchGlobalScale = view->scale() == compositor()->globalScale();

        // Cache view pos
        view->currentRectC.setPos(*view->posC());

        // Make view pos fit output pixels grid
        if (scale() != compositor()->globalScale())
        {
            view->currentRectC.setX(view->currentRectC.x() - view->currentRectC.x() % compositor()->globalScale());
            view->currentRectC.setY(view->currentRectC.y() - view->currentRectC.y() % compositor()->globalScale());
        }

        // Cache view size
        view->currentRectC.setSize(*view->sizeC());

        // We clear damage only
        bool clearDamage = true;

        // Update view intersected outputs
        for (LOutput *o : compositor()->outputs())
        {
            if (o->rectC().intersects(view->currentRectC, false))
            {
                view->enterOutput((Output*)o);

                // If the view is visible on another output and has not rendered the new damage
                // prevent clearing the damage immediately
                if (o != this && (view->outputsMap[o].lastRenderedDamageId < view->damageId()))
                {
                    clearDamage = false;
                    o->repaint();
                }
            }
            else
               view->leaveOutput((Output*)o);
        }

        bool rectChanged = view->currentRectC!= view->currentOutputData->previousRectC;

        // If rect or order changed (set current rect and prev rect as damage)
        if (rectChanged || view->currentOutputData->changedOrder)
        {
            view->currentDamageTransposedC.clear();
            view->currentDamageTransposedC.addRect(view->currentRectC);
            view->currentDamageTransposedC.addRect(view->currentOutputData->previousRectC);
            view->currentOutputData->changedOrder = false;
            view->currentOutputData->previousRectC = view->currentRectC;
        }
        else
        {
            view->currentDamageTransposedC = *view->damageC();
            view->currentDamageTransposedC.offset(view->currentRectC.pos());
        }

        // Remove previus opaque region to view damage
        view->currentDamageTransposedC.subtractRegion(opaqueTransposedCSum);

        // Add clipped damage to new damage
        newDamage.addRegion(view->currentDamageTransposedC);

        // Store tansposed traslucent region
        view->currentTraslucentTransposedC = *view->translucentRegionC();
        view->currentTraslucentTransposedC.offset(view->currentRectC.pos());

        // Store tansposed opaque region
        view->currentOpaqueTransposedC = *view->opaqueRegionC();
        view->currentOpaqueTransposedC.offset(view->currentRectC.pos());

        // Store sum of previus opaque regions
        view->currentOpaqueTransposedCSum = opaqueTransposedCSum;

        // Check if view is ocludded
        LRegion ocluddedTest;
        ocluddedTest.addRect(view->currentRectC);
        ocluddedTest.subtractRegion(opaqueTransposedCSum);
        view->occluded = ocluddedTest.empty();

        if (!view->occluded && clearDamage)
            view->requestNextFrame();

        // Add current transposed opaque region to global sum
        opaqueTransposedCSum.addRegion(view->currentOpaqueTransposedC);

        view->currentOutputData->lastRenderedDamageId = view->damageId();

        return;
    }

    // Check the render mode
    switch (view->renderMode())
    {
        case VIEW_RENDER_MODE_ITSELF_ONLY:
        {
            calculateNewDamage(view, true);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_ONLY:
        {
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                calculateNewDamage(*it);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_AFTER:
        {
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                calculateNewDamage(*it);
            calculateNewDamage(view, true);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_FIRST:
        {
            calculateNewDamage(view, true);
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                calculateNewDamage(*it);
        }break;
    }
}

void Output::drawOpaqueDamage(View *view, bool force)
{
    if (!view->isRenderable || view->occluded)
        return;

    if (force)
    {
        view->currentOpaqueTransposedC.intersectRegion(newDamage);
        view->currentOpaqueTransposedC.subtractRegion(view->currentOpaqueTransposedCSum);

        boxes = view->currentOpaqueTransposedC.rects(&n);

        // Draw opaque rects
        for (Int32 i = 0; i < n; i++)
        {
            w = boxes->x2 - boxes->x1;
            h = boxes->y2 - boxes->y1;

            p->drawTextureC(
                (LTexture*)view->texture(),
                boxes->x1 - view->currentRectC.x(),
                boxes->y1 - view->currentRectC.y(),
                w,
                h,
                boxes->x1,
                boxes->y1,
                w,
                h,
                view->bufferScaleMatchGlobalScale ? 0.0 : view->scale());

            boxes++;
        }

        return;
    }

    // Check the render mode
    switch (view->renderMode())
    {
        case VIEW_RENDER_MODE_ITSELF_ONLY:
        {
            drawOpaqueDamage(view, true);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_ONLY:
        {
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                drawOpaqueDamage(*it);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_AFTER:
        {
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                drawOpaqueDamage(*it);
            drawOpaqueDamage(view, true);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_FIRST:
        {
            drawOpaqueDamage(view, true);
            for (list<View*>::const_reverse_iterator it = view->children().rbegin(); it != view->children().rend(); it++)
                drawOpaqueDamage(*it);
        }break;
    }
}

void Output::drawTranslucentDamage(View *view, bool force)
{
    if (!view->isRenderable || view->occluded)
        return;

    if (force)
    {
        view->occluded = true;
        view->currentTraslucentTransposedC.intersectRegion(newDamage);
        view->currentTraslucentTransposedC.subtractRegion(view->currentOpaqueTransposedCSum);

        boxes = view->currentTraslucentTransposedC.rects(&n);

        // Draw transulcent rects

        for (Int32 i = 0; i < n; i++)
        {
            w = boxes->x2 - boxes->x1;
            h = boxes->y2 - boxes->y1;

            p->drawTextureC(
                (LTexture*)view->texture(),
                boxes->x1 - view->currentRectC.x(),
                boxes->y1 - view->currentRectC.y(),
                w,
                h,
                boxes->x1 ,
                boxes->y1,
                w,
                h,
                view->bufferScaleMatchGlobalScale ? 0.0 : view->scale());

            boxes++;
        }
        return;
    }

    // Check the render mode
    switch (view->renderMode())
    {
        case VIEW_RENDER_MODE_ITSELF_ONLY:
        {
            drawTranslucentDamage(view, true);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_ONLY:
        {
            for (View *child : view->children())
                drawTranslucentDamage(child);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_AFTER:
        {
            drawTranslucentDamage(view, true);
            for (View *child : view->children())
                drawTranslucentDamage(child);
        }break;
        case VIEW_RENDER_MODE_CHILDREN_FIRST:
        {
            for (View *child : view->children())
                drawTranslucentDamage(child);
            drawTranslucentDamage(view, true);
        }break;
    }
}

void Output::initializeGL()
{
    c = (Compositor*)compositor();
    p = painter();

    fullDamage();
    repaint();
}

void Output::resizeGL()
{
    Int32 x = 0;

    // Set double scale to outputs with DPI >= 120
    for (Output *output : (std::list<Output*>&)compositor()->outputs())
    {
        if (output->dpi() >= 120)
            output->setScale(2);
        else
            output->setScale(1);

        output->setPosC(LPoint(x, 0));
        output->fullDamage();
        output->repaint();
        x += output->rectC().w();
    }
}

void repaintParent(LSurface *s)
{
    if (s)
    {
        s->requestNextFrame();
        repaintParent(s->parent());
    }
}

void repaintChildren(LSurface *s)
{
    for (LSurface *c : s->children())
    {
        c->requestNextFrame();
        repaintChildren(c);
    }
}

void Output::paintGL()
{
    opaqueTransposedCSum.clear();

    // If output rect changed damage entire screen
    if (lastRectC != rectC())
    {
        fullDamage();
        lastRectC = rectC();
    }

    // Check if surface moved under cursor
    if (seat()->pointer()->surfaceAtC(cursor()->posC()) != seat()->pointer()->focusSurface())
        seat()->pointer()->pointerPosChangeEvent(
            cursor()->posC().x(),
            cursor()->posC().y());

    if (seat()->dndManager()->icon())
        compositor()->raiseSurface(seat()->dndManager()->icon()->surface());

    calculateNewDamage(c->mainView);

    // Save new damage for next frame and add old damage to current damage
    if (buffersCount() > 1)
    {
        LRegion oldDamage = damage;
        damage = newDamage;
        newDamage.addRegion(oldDamage);
    }

    glDisable(GL_BLEND);
    drawOpaqueDamage(c->mainView);

    // Background
    LRegion backgroundDamage = newDamage;
    backgroundDamage.subtractRegion(opaqueTransposedCSum);
    boxes = backgroundDamage.rects(&n);

    for (Int32 i = 0; i < n; i++)
    {
        p->drawColorC(boxes->x1,
                      boxes->y1,
                      boxes->x2 - boxes->x1,
                      boxes->y2 - boxes->y1,
                      0.15f,
                      0.25f,
                      0.35f,
                      1.f);
        boxes++;
    }

    glEnable(GL_BLEND);

    // Traslucent

    drawTranslucentDamage(c->mainView);

    setBufferDamageC(newDamage);
    newDamage.clear();
}
