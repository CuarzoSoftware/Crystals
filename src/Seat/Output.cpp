#include <Louvre/Cursor/LCursor.h>
#include <Louvre/LCompositor.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Seat/Output.h>
#include <Roles/Surface.h>
#include <Scene/Scene.h>

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

    auto style { welcome.textStyle() };
    style.setFontSize(32);
    welcome.setTextStyle(style);
}

void Output::initializeGL()
{
    res = std::make_unique<Resources>(*this);
    initLayers();
}

void Output::paintGL()
{
    res->ignoreKayRepaintCalls = true;
    syncSurfaceViews();
    res->ignoreKayRepaintCalls = false;

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
    GetScene()->scene->render(target);

    handleSurfaceCallbacks();
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
        res->layers[i].layout().setPosition(YGEdgeLeft, pos().y());
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
