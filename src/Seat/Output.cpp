#include "LBaseSurfaceRole.h"
#include <Core/CZCore.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/LCompositor.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Seat/Output.h>
#include <Roles/Surface.h>
#include <Effects/SurfaceBlurManager.h>
#include <Core/CaptureManager.h>
#include <Scene/Scene.h>
#include <Effects/MinimizeAnimation.h>
#include <Effects/FullscreenAnimation.h>
#include <Core/Workspaces.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/CZLinearAnimation.h>
#include <Core/CZEase.h>

Output::Resources::Resources(Output &output) noexcept : output(output)
{
    auto *scene { GetScene() };

    target = scene->scene->makeTarget();
    target->setClearColor(SK_ColorBLACK);
    target->userData = &output;

    target->onMarkedDirty.subscribe(this, [this](auto &){
        if (!ignoreKayRepaintCalls)
            this->output.repaint();
    });
}

void Output::initializeGL()
{
    res = std::make_unique<Resources>(*this);
    initLayers();
}

void Output::paintGL()
{
    auto core { CZCore::Get() };
    core->updateAnimations();

    // Content of outputs switching workspaces is only drawn on them
    Workspaces::BeginRender(this);

    res->ignoreKayRepaintCalls = true;
    syncSurfaceViews();
    TopbarAutoHide::SyncMirrors(this);
    res->ignoreKayRepaintCalls = false;

    // Bake (un)minimizing toplevels (needs this thread's GL context) before rendering.
    MinimizeAnimation::ProcessPending(*this);

    auto surface { RSurface::WrapImage(image()) };
    surface->setGeometry({
        .viewport = SkRect::Make(rect()),
        .dst = SkRect::Make(image()->size()),
        .transform = transform()
    });

    auto target { res->target };
    target->surface = surface;
    target->age = imageAge();
    target->setBakedNodesScale(scale());
    target->outDamage = &damage;

    // Without a cursor plane Louvre paints the cursor on top: what was under it must be repainted
    // (empty while oversampling, Louvre restores it then).
    target->inDamage = &softwareCursorDamage();
    GetScene()->scene->render(target);
    Workspaces::EndRender();

    handleSurfaceCallbacks();
    updateCapturedBlurs();

    // Keep repainting only while animations are actually running. animationCount() also counts
    // stopped-but-still-registered (non-oneshot) animations, which would busy-loop repaints at the
    // display's refresh rate forever (burning CPU at idle).
    if (core->hasRunningAnimations())
        repaint();
}

void Output::moveGL()
{
    updateLayers();
    repaint();
}

void Output::resizeGL()
{
    updateLayers();
    repaint();
}

void Output::uninitializeGL()
{
    res.reset();
}

void Output::initLayers() noexcept
{
    for (int i = 0; i < LLayerOverlay + 1; i++)
    {
        const SkRegion empty;
        res->layers[i].setInputRegion(&empty); // Structure only (see Scene)
        res->layers[i].layout().setPositionType(YGPositionTypeAbsolute);
        res->layers[i].layout().setAlignItems(YGAlignCenter);
        res->layers[i].layout().setJustifyContent(YGJustifyCenter);
        res->layers[i].setParent(&GetScene()->layers[i]);
    }

    updateLayers();
}

void Output::updateLayers() noexcept
{
    for (int i = 0; i < LLayerOverlay + 1; i++)
    {
        res->layers[i].layout().setPosition(YGEdgeLeft, pos().x());
        res->layers[i].layout().setPosition(YGEdgeTop, pos().y());
        res->layers[i].layout().setWidth(rect().width());
        res->layers[i].layout().setHeight(rect().height());
    }
}

void Output::syncSurfaceViews() noexcept
{
    for (Surface *surface : GetSurfaces())
    {
        surface->view.syncVisibility();

        if (!surface->view.visible())
            continue;

        surface->view.syncPos();
    }
}

void Output::updateCapturedBlurs() noexcept
{
    // The blur behind a window is part of its captures (see ToplevelRole::imageCaptureFrameRequest())
    for (Surface *surface : GetSurfaces())
    {
        auto *blur { static_cast<SurfaceBlurManager*>(surface->backgroundBlur()) };

        if (!blur->blurred)
            continue;

        blur->blurred = false;

        LSurface *root { surface };

        while (root->subsurface() && root->parent())
            root = root->parent();

        if (root->toplevel())
            CaptureManager::ToplevelChanged(*root->toplevel());
    }
}

void Output::handleSurfaceCallbacks() noexcept
{
    for (Surface *surface : GetSurfaces())
    {
        // A commit without new damage (e.g. a layout change that renders the same pixels) doesn't
        // repaint the view, so it's never "rendered": answer its frame callbacks anyway, otherwise
        // the client waits for them forever and stops updating (e.g. a dock animation stalling).
        const bool idleCommit { !surface->hasDamage() && surface->view.visible() && !surface->view.intersectedTargets().empty() };

        // Drawn by a fullscreen animation: it keeps drawing (its new size is awaited)
        if (surface->view.view.renderedOnLastTarget() || idleCommit || surface->cursorRole() || FullscreenAnimation::WantsFrames(surface))
        {
            surface->view.view.clearRenderedOnLastTarget();
            surface->requestNextFrame();
        }

        // Hidden top bars (see TopbarAutoHide) stay on their output (e.g. keeping their scale)
        if (TopbarAutoHide::HidesSurface(surface) || Workspaces::IsParked(surface) || FullscreenAnimation::HidesSurface(surface))
            continue;

        bool outputFound;

        for (LOutput *output : compositor()->outputs())
        {
            outputFound = false;

            for (auto *target : surface->view.intersectedTargets())
            {
                if (target->userData == output)
                {
                    outputFound = true;
                    break;
                }
            }

            if (outputFound)
                surface->sendOutputEnterEvent(output);
            else
                surface->sendOutputLeaveEvent(output);
        }
    }
}
