#include <Effects/ResizeBlackout.h>
#include <Roles/LayerRole.h>
#include <Roles/Surface.h>
#include <Seat/Output.h>
#include <Scene/Scene.h>
#include <Core/Crystals.h>
#include <Core/CZEase.h>

ResizeBlackout::ResizeBlackout(Output &output) noexcept :
    m_output(output),
    m_black(CZAdaptiveColor(SK_ColorBLACK), &GetScene()->blackouts),
    m_timeout([this](CZTimer*){ end(); }), // A client never caught up: shown anyway
    m_fade(FadeMs,
        [this](CZAnimation *a){ m_black.setOpacity(1.f - SkScalar(CZEase::OutCubic(a->value()))); },
        [this](CZAnimation*){ m_black.setVisible(false); })
{
    const SkRegion empty;
    m_black.setInputRegion(&empty); // Only drawn: input goes on as usual
    m_black.layout().setPositionType(YGPositionTypeAbsolute);
    m_black.setVisible(false);
}

ResizeBlackout::~ResizeBlackout() noexcept
{
    m_timeout.stop();
    m_fade.pause();
}

void ResizeBlackout::begin() noexcept
{
    m_fade.pause();
    m_black.setOpacity(1.f);
    m_black.setVisible(true);
    placeBlack();
    m_waiting = true;
    m_timeout.start(TimeoutMs); // Restarted if resized again
    m_output.repaint();
}

void ResizeBlackout::update() noexcept
{
    if (!m_black.visible())
        return;

    placeBlack();

    if (m_waiting && settled())
        end();
}

void ResizeBlackout::SurfaceResized(LSurface *surface) noexcept
{
    if (!surface || surface->parent() || !surface->layerRole())
        return;

    if (auto *output { static_cast<Output*>(surface->layerRole()->exclusiveOutput()) }; output && output->blackout)
        output->blackout->update();
}

bool ResizeBlackout::settled() const noexcept
{
    // The desktop shell of this output: each committed the size it was configured with
    for (Surface *s : GetSurfaces())
    {
        if (s->parent() || !s->mapped() || !s->layerRole() || s->layerRole()->exclusiveOutput() != &m_output)
            continue;

        if (!Crystals::IsDesk(s) && !Crystals::IsShell(s))
            continue;

        if (!static_cast<LayerRole*>(s->layerRole())->settled())
            return false;
    }

    return true;
}

void ResizeBlackout::end() noexcept
{
    if (!m_waiting)
        return;

    m_waiting = false;
    m_timeout.stop();
    m_fade.reset();
    m_fade.start();
    m_output.repaint();
}

void ResizeBlackout::placeBlack() noexcept
{
    const SkIRect &r { m_output.rect() };
    m_black.layout().setPosition(YGEdgeLeft, r.x());
    m_black.layout().setPosition(YGEdgeTop, r.y());
    m_black.layout().setWidth(r.width());
    m_black.layout().setHeight(r.height());
}
