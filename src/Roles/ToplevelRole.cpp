#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Effects/MinimizeAnimation.h>
#include <Core/Client.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <Core/CaptureManager.h>
#include <Core/Crystals.h>
#include <Louvre/Manager/LImageCaptureSession.h>
#include <Louvre/Manager/LImageCaptureFrame.h>
#include <Louvre/Roles/LSubsurfaceRole.h>
#include <CZ/Ream/RPainter.h>
#include <CZ/Ream/RPass.h>
#include <CZ/Ream/RSurface.h>
#include <functional>
#include <algorithm>

ToplevelRole::ToplevelRole(const void *params) noexcept : LToplevelRole(params) {}

ToplevelRole::~ToplevelRole()
{
    m_ssd.reset();

    if (auto *surf { static_cast<Surface*>(surface()) })
        surf->view.setBottomRadius(0);
}

LToplevelRole::DecorationMode ToplevelRole::wantedDecorationMode() const noexcept
{
    // Ours unless the client prefers its own (clients without xdg-decoration stay client-side).
    return preferredDecorationMode() == ClientSide ? ClientSide : ServerSide;
}

Int32 ToplevelRole::titlebarHeight() const noexcept
{
    return m_ssd ? SSDTheme::TitlebarHeight : 0;
}

Int32 ToplevelRole::pendingTitlebarHeight() const noexcept
{
    // configureDecorationMode() falls back to client-side for clients without xdg-decoration, so the
    // pending mode is what the window will actually get.
    const auto &pending { pendingConfiguration() };
    return pending.decorationMode == ServerSide && !pending.windowState.has(CZWinFullscreen) ? SSDTheme::TitlebarHeight : 0;
}

SkIPoint ToplevelRole::rolePos() const
{
    return LToplevelRole::rolePos() + SkIPoint::Make(0, titlebarHeight());
}

void ToplevelRole::configureAvailableBounds() noexcept
{
    // The largest size the content may have: the available area minus the title bar.
    LOutput *output { exclusiveOutput() ? exclusiveOutput() : cursor()->output() };

    if (!output)
        return;

    SkISize bounds { output->availableGeometry().size() };
    bounds.fHeight = std::max(1, bounds.height() - pendingTitlebarHeight());
    configureBounds(bounds);
}

void ToplevelRole::configureMaximizedSize() noexcept
{
    const auto &pending { pendingConfiguration() };

    if (!pending.windowState.has(CZWinMaximized) || pending.windowState.has(CZWinFullscreen))
        return;

    if (!exclusiveOutput())
        if (LOutput *output { cursor()->output() })
            setExclusiveOutput(output);

    if (!exclusiveOutput())
        return;

    // The available area holds the title bar too.
    SkISize size { exclusiveOutput()->availableGeometry().size() };
    size.fHeight = std::max(1, size.height() - pendingTitlebarHeight());
    configureSize(size);
}

void ToplevelRole::configureRequest()
{
    // The decoration mode first: the sizes below depend on it.
    LToplevelRole::configureRequest();
    configureDecorationMode(wantedDecorationMode());
    configureAvailableBounds();

    // Asked to be maximized before mapping (e.g. an app restoring its maximized state): the default
    // configuration lets the client choose its size, give it the maximized one instead.
    configureMaximizedSize();
}

void ToplevelRole::preferredDecorationModeChanged()
{
    // Some clients ask for server-side decorations only after their first configure: maximized
    // ones must shrink to make room for the title bar (or grow back without it).
    const DecorationMode prev { pendingConfiguration().decorationMode };
    configureDecorationMode(wantedDecorationMode());

    if (pendingConfiguration().decorationMode != prev)
    {
        configureAvailableBounds();
        configureMaximizedSize();
    }
}

void ToplevelRole::setMaximizedRequest()
{
    LToplevelRole::setMaximizedRequest();
    configureMaximizedSize();
}

void ToplevelRole::unsetFullscreenRequest()
{
    // Back to maximized (if it was): with the title bar again.
    LToplevelRole::unsetFullscreenRequest();
    configureMaximizedSize();
}

void ToplevelRole::fitIfYoung() noexcept
{
    static constexpr auto FitWindowMs { std::chrono::milliseconds(2000) };
    static constexpr UInt32 MaxFitAttempts { 3 }; // Some can't shrink (e.g. a large minimum size)

    if (decorationMode() != ServerSide || isMaximized() || isFullscreen() || resizeSession().isActive() ||
        m_fitAttempts >= MaxFitAttempts || std::chrono::steady_clock::now() - m_created > FitWindowMs)
        return;

    LOutput *output { cursor()->output() };

    if (!output)
        return;

    // The whole window (content + title bar) must fit the available area.
    const SkISize avail { output->availableGeometry().size() };
    const SkISize size { windowGeometry().size() };
    const SkISize fit { std::min(size.width(), avail.width()), std::min(size.height(), avail.height() - SSDTheme::TitlebarHeight) };

    if (size.isEmpty() || fit == size || fit.isEmpty())
        return;

    m_fitAttempts++;
    m_fitPending = true;
    configureSize(fit);
}

void ToplevelRole::unmaximizeInPlace() noexcept
{
    if (!isMaximized())
        return;

    // Where it is now: Louvre puts un-maximized windows back at prevRect (see stateChanged())
    prevRect = SkIRect::MakePtSize(surface()->pos(), windowGeometry().size());
    configureSize(windowGeometry().size());
    configureState(pendingConfiguration().windowState & ~CZWinMaximized);
}

void ToplevelRole::placeMapped() noexcept
{
    auto *surf { surface() };
    LOutput *output { exclusiveOutput() ? exclusiveOutput() : cursor()->output() };

    if (!surf || !output)
        return;

    const SkIRect avail { SkIRect::MakePtSize(output->pos() + output->availableGeometry().topLeft(), output->availableGeometry().size()) };

    if (isFullscreen())
    {
        surf->setPos(output->pos());
        return;
    }

    // The surface position is the top-left of the whole window, title bar included (see rolePos()).
    if (isMaximized())
    {
        surf->setPos(avail.topLeft());
        return;
    }

    const SkISize size { windowGeometry().width(), windowGeometry().height() + titlebarHeight() };
    surf->setPos(avail.x() + (avail.width() - size.width()) / 2, avail.y() + (avail.height() - size.height()) / 2);

    if (surf->pos().y() < avail.y())
        surf->setY(avail.y());
}

void ToplevelRole::titleChanged()
{
    LToplevelRole::titleChanged();

    if (m_ssd)
        m_ssd->setTitle(title());
}

void ToplevelRole::updateDecorations() noexcept
{
    auto *surf { static_cast<Surface*>(surface()) };

    if (!surf)
        return;

    const bool wanted { decorationMode() == ServerSide && !isFullscreen() };

    if (wanted && !m_ssd)
    {
        m_ssd = std::make_unique<SSD>(*this);
        surf->view.setBottomRadius(SSDTheme::BorderRadius);
    }
    else if (!wanted && m_ssd)
    {
        m_ssd.reset();
        surf->view.setBottomRadius(0);
    }
    else if (m_ssd)
        m_ssd->update();

    enforceMinSize();
    surf->view.syncPos();
}

void ToplevelRole::enforceMinSize() noexcept
{
    // Decorated windows smaller than the decorations allow (their shadow corners would overlap) are
    // asked to grow, once per undersized state (some can't, e.g. fixed size dialogs).
    const SkISize size { surface()->size() };
    const SkISize min { SSD::MinSize() };

    if (!m_ssd || size.isEmpty() || (size.width() >= min.width() && size.height() >= min.height()))
    {
        m_minSizeRequested = false;
        return;
    }

    if (m_minSizeRequested || isMaximized() || isFullscreen() || resizeSession().isActive())
        return;

    const SkISize wanted { std::max(size.width(), min.width()), std::max(size.height(), min.height()) };

    if ((maxSize().width() != 0 && maxSize().width() < wanted.width()) ||
        (maxSize().height() != 0 && maxSize().height() < wanted.height()))
        return;

    m_minSizeRequested = true;
    configureSize(wanted);
}

void ToplevelRole::minimizedChanged()
{
    LToplevelRole::minimizedChanged();

    auto *surf { static_cast<Surface*>(surface()) };

    if (surf->mapped())
        MinimizeAnimation::Start(*this, isMinimized());

    surf->view.syncVisibility();
}

void ToplevelRole::appIdChanged()
{
    LToplevelRole::appIdChanged();

    // Keep the bar's view of the active app in sync (e.g. an app id set after activation).
    if (seat()->activeToplevel() == this)
        static_cast<Client*>(client())->setActiveHeavenClient(this);
}

void ToplevelRole::stateChanged(CZBitset<Changes> changes, const State &prev)
{
    LToplevelRole::stateChanged(changes, prev);
    updateDecorations();

    // Young windows that don't fit once decorated (see fitIfYoung()). Once refitted, centered again.
    if (changes.has(DecorationModeChanged | WindowGeometryChanged))
    {
        if (m_fitPending && changes.has(WindowGeometryChanged) && surface()->mapped())
        {
            m_fitPending = false;
            placeMapped();
        }

        fitIfYoung();
    }

    // Decorations shown/hidden while maximized: keep the window (now with or without its title bar)
    // at the top of the available area.
    if (changes.has(DecorationModeChanged) && isMaximized() && !isFullscreen() && exclusiveOutput())
        surface()->setPos(exclusiveOutput()->pos() + exclusiveOutput()->availableGeometry().topLeft());

    // Activated (new, clicked, or asked by e.g. the dock): it also gets the keyboard focus, which
    // apps (e.g. Qt ones) treat as being the active window.
    if (changes.has(StateChanged) && isActivated() && !prev.windowState.has(CZWinActivated))
    {
        auto *keyboard { seat()->keyboard() };

        if (!keyboard->grab() && !surface()->hasKeyboardFocus())
            keyboard->setFocus(surface());
    }
}

void ToplevelRole::imageCaptureFrameRequest(LImageCaptureFrame *frame)
{
    if (!CaptureManager::IsThumbnail(frame->session()))
    {
        LToplevelRole::imageCaptureFrameRequest(frame);
        return;
    }

    auto target { frame->surface() };

    if (!target)
    {
        frame->fail(LImageCaptureFrame::Unknown);
        return;
    }

    const SkISize bufferSize { frame->session().bufferSize() };
    const SkRegion region { SkIRect::MakeSize(bufferSize) }; // Small: always redrawn whole

    // The window (title bar included) in logical units, scaled to the buffer
    SkISize window { windowGeometry().isEmpty() ? surface()->size() : windowGeometry().size() };
    const Int32 titlebar { titlebarHeight() };
    window.fHeight += titlebar;

    const SkScalar sx { SkScalar(bufferSize.width()) / SkScalar(std::max(1, window.width())) };
    const SkScalar sy { SkScalar(bufferSize.height()) / SkScalar(std::max(1, window.height())) };

    auto pass { target->beginPass(RPassCap_Painter) };

    if (!pass)
    {
        frame->fail(LImageCaptureFrame::Unknown);
        return;
    }

    auto *painter { pass->getPainter() };
    painter->setBlendMode(RBlendMode::Src);
    painter->setColor(SK_ColorTRANSPARENT);
    painter->drawColor(region);

    // Title bar (light or dark, like the decorations), with its separator
    const Int32 barH { SkScalarRoundToInt(titlebar * sy) };

    if (barH > 0)
    {
        const CZColorScheme scheme { GetCompositor()->colorScheme() };
        painter->setColor(SSDTheme::TitlebarActive.resolve(scheme));
        painter->drawColor(SkRegion(SkIRect::MakeWH(bufferSize.width(), barH)));
        painter->setBlendMode(RBlendMode::SrcOver);
        painter->setColor(SSDTheme::Separator.resolve(scheme));
        painter->drawColor(SkRegion(SkIRect::MakeXYWH(0, barH - 1, bufferSize.width(), 1)));
    }

    painter->setBlendMode(RBlendMode::SrcOver);

    // Content below it: back to front, subsurfaces below, the surface, subsurfaces above
    const SkIPoint origin { -windowGeometry().x(), -windowGeometry().y() + titlebar };

    const std::function<void(LSurface*, SkIPoint)> draw = [&](LSurface *s, SkIPoint pos)
    {
        for (auto *sub : s->subsurfacesBelow())
            if (sub->surface()->mapped())
                draw(sub->surface(), pos + sub->localPos());

        if (auto image { s->image() })
        {
            RDrawImageInfo info {};
            info.image = image;
            info.src = s->srcRect();
            info.srcScale = s->scale();
            info.srcTransform = s->bufferTransform();
            info.dst = SkIRect::MakeLTRB(
                SkScalarRoundToInt(pos.x() * sx),
                SkScalarRoundToInt(pos.y() * sy),
                SkScalarRoundToInt((pos.x() + s->size().width()) * sx),
                SkScalarRoundToInt((pos.y() + s->size().height()) * sy));
            info.minFilter = info.magFilter = RImageFilter::Linear;
            painter->drawImage(info, &region);
        }

        for (auto *sub : s->subsurfacesAbove())
            if (sub->surface()->mapped())
                draw(sub->surface(), pos + sub->localPos());
    };

    draw(surface(), origin);
    pass.reset();
    frame->ready();
}
