#include <Seat/Output.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>
#include <RSurface.h>
#include <LCursor.h>
#include <LCompositor.h>
#include <RPass.h>

Output::Resources::Resources(Output &output) noexcept : output(output)
{
    auto *scene { GetScene() };
    scene->layers[LLayerOverlay].enableChildrenClipping(false);

    target = scene->scene->makeTarget();
    target->setClearColor(SK_ColorWHITE);
    target->userData = &output;

    target->onMarkedDirty.subscribe(this, [this](auto &){
        if (!ignoreKayRepaintCalls)
            this->output.repaint();
    });
}

void Output::initializeGL()
{
    res = std::make_unique<Resources>(*this);
    //setScale(1.5f);
    //enableFractionalOversampling(false);
}

void Output::paintGL()
{
    res->ignoreKayRepaintCalls = true;
    syncSurfaceViews();
    res->ignoreKayRepaintCalls = false;

    auto surface { RSurface::WrapImage(currentImage()) };
    surface->setGeometry({
        .viewport = SkRect::Make(rect()),
        .dst = SkRect::Make(realBufferSize()),
        .transform = transform()
    });

    auto target { res->target };
    target->surface = surface;
    target->age = imageAge();
    target->setBakedNodesScale(scale());
    target->outDamage = &damage;
    GetScene()->scene->render(target);

    handleSurfaceCallbacks();
}

void Output::moveGL()
{
    repaint();
}

void Output::resizeGL()
{
    repaint();
}

void Output::uninitializeGL()
{
    res.reset();
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

void Output::handleSurfaceCallbacks() noexcept
{
    for (Surface *surface : GetSurfaces())
    {
        if (surface->view.view.renderedOnLastTarget() || surface->cursorRole())
        {
            surface->view.view.clearRenderedOnLastTarget();
            surface->requestNextFrame();
        }

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

void Output::addCursorDamage() noexcept
{
    /* When hw planes are not available */

    /*
    const LRegion &cursorDamage { cursor()->damage(this) };

    if (cursorDamage.empty())
        res->sceneTarget->inDamageRegion = nullptr;
    else
    {
        Converter::LtSRegion(cursorDamage, &res->inDamage);
        res->sceneTarget->inDamageRegion = &res->inDamage;
    }*/
}

