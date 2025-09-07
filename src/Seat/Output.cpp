#include "AKApp.h"
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

    testBlurContainer.setParent(&scene->layers[LLayerTop]);
    testBlurContainer.layout().setPosition(YGEdgeLeft, 400);
    testBlurContainer.layout().setPosition(YGEdgeTop, 400);
    testBlurContainer.layout().setWidth(800);
    testBlurContainer.layout().setHeight(800);

    blurFX.setRoundRectClip(
        AKRRect(SkIRect::MakeWH(800, 800),
                100, 10, 0, 30));

    /*SkPath p;
    p.addCircle(200, 200, 200);
    blurFX.setPathClip(p);*/

    /*
    testSolidColor.setParent(&scene->layers[LLayerOverlay]);
    testSolidColor.layout().setPosition(YGEdgeLeft, 500.f);
    testSolidColor.layout().setPosition(YGEdgeTop, 500.f);
    testSolidColor.layout().setWidth(200.f);
    testSolidColor.layout().setHeight(200.f);
    testSolidColor.setOpacity(0.3f);

    testText.setParent(&scene->layers[LLayerOverlay]);
    testText.layout().setPosition(YGEdgeLeft, 100.f);
    testText.layout().setPosition(YGEdgeTop, 100.f);
    testText.enableReplaceImageColor(true);
    testText.setColor(SK_ColorMAGENTA);
    auto style { testText.textStyle() };
    style.setFontSize(64.f);
    style.setFontFamilies({SkString("Playwrite MX Guides")});
    testText.setTextStyle(style);*/

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
    //setScale(2.25f);
    //enableFractionalOversampling(false);
}

void Output::paintGL()
{
    res->ignoreKayRepaintCalls = true;
    syncSurfaceViews();

    res->testBlurContainer.layout().setPosition(YGEdgeLeft, cursor()->pos().x());
    res->testBlurContainer.layout().setPosition(YGEdgeTop, cursor()->pos().y());

    res->ignoreKayRepaintCalls = false;

    auto surface { RSurface::WrapImage(currentImage()) };
    surface->setGeometry({
        .viewport = SkRect::Make(rect()),
        .dst = SkRect::Make(realBufferSize()),
        .transform = transform()
    });


    /*auto pass { surface->beginPass() };
    pass->getPainter()->clear();
    pass.reset();*/

    auto target { res->target };
    target->surface = surface;
    target->age = imageAge();
    target->setBakedNodesScale(scale());
    target->outDamage = &damage;
    GetScene()->scene->render(target);

    /*
    const bool trackingDamage { hasBufferDamageSupport() || usingFractionalScale() };
    res->sceneTarget->outDamageRegion = trackingDamage ? &res->outDamage : nullptr;
    res->sceneTarget->setSurface(Converter::SkSurfaceFromOutput(this));
    res->sceneTarget->setAge(currentBufferAge());
    res->sceneTarget->setBakedComponentsScale(scale());
    res->sceneTarget->setTransform(static_cast<AKTransform>(transform()));
    res->sceneTarget->setViewport(SkRect::MakeXYWH(pos().x(), pos().y(), size().w(), size().h()));
    res->sceneTarget->setDstRect(SkIRect::MakeXYWH(0, 0, realBufferSize().w(), realBufferSize().h()));
    addCursorDamage();
    G::scene()->render(res->sceneTarget);

    if (trackingDamage)
        handleOutDamage();*/

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

