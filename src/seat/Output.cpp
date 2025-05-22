#include <AK/AKLog.h>
#include <seat/Output.h>
#include <core/Application.h>
#include <utils/Converter.h>
#include <roles/Surface.h>
#include <LCursor.h>
#include <LCompositor.h>

Output::Resources::Resources(Output &output) noexcept : output(output)
{
    sceneTarget = G::scene()->createTarget();
    sceneTarget->setUserData(&output);
    sceneTarget->setClearColor(SK_ColorBLACK);

    // Triggers repaint when AKNodes become dirty
    sceneTarget->on.markedDirty.subscribe(this, [this](auto &){
        if (!ignoreKayRepaintCalls)
            this->output.repaint();
    });
}

Output::Resources::~Resources()
{
    G::scene()->destroyTarget(sceneTarget);
    G::app()->freeGLContextData();
}

void Output::initializeGL()
{
    res = std::make_unique<Resources>(*this);
}

void Output::paintGL()
{
    res->ignoreKayRepaintCalls = true;
    syncSurfaceViews();
    res->ignoreKayRepaintCalls = false;

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
        handleOutDamage();

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
    for (Surface *surface : G::surfaces())
    {
        surface->view.syncVisibility();

        if (!surface->view.visible())
            continue;

        surface->view.syncPos();
        surface->view.syncTexture();
    }
}

void Output::handleSurfaceCallbacks() noexcept
{
    for (Surface *surface : G::surfaces())
    {
        if (surface->view.renderedOnLastTarget())
        {
            surface->view.clearRenderedOnLastTarget();
            surface->requestNextFrame();
        }

        bool outputFound;

        for (LOutput *output : compositor()->outputs())
        {
            outputFound = false;

            for (auto *target : surface->view.intersectedTargets())
            {
                if (target->userData() == output)
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

    const LRegion &cursorDamage { cursor()->damage(this) };

    if (cursorDamage.empty())
        res->sceneTarget->inDamageRegion = nullptr;
    else
    {
        Converter::LtSRegion(cursorDamage, &res->inDamage);
        res->sceneTarget->inDamageRegion = &res->inDamage;
    }
}

void Output::handleOutDamage() noexcept
{
    if (hasBufferDamageSupport() || usingFractionalScale())
    {
        if (!usingFractionalScale())
            res->outDamage.translate(pos().x(), pos().y());

        LRegion damage;
        Converter::StLRegion(res->outDamage, &damage);
        setBufferDamage(&damage);
    }
}
