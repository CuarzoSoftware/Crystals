#include <Core/SessionLock.h>
#include <Roles/ToplevelRole.h>
#include <Core/Attention.h>
#include <Roles/Surface.h>
#include <Effects/MinimizeAnimation.h>
#include <Effects/TopbarAutoHide.h>
#include <Effects/FullscreenAnimation.h>
#include <Core/Workspaces.h>
#include <Effects/Expose.h>
#include <Effects/ShowDesktop.h>
#include <Core/Client.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Seat/LPointer.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <Core/CaptureManager.h>
#include <Effects/SurfaceBlurManager.h>
#include <CZ/skia/core/SkCanvas.h>
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
    Workspaces::Removed(*this);
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
    // Fullscreen: the title bar slides over the content when revealed (see SSD)
    return m_ssd && !isFullscreen() ? SSDTheme::TitlebarHeight : 0;
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
    // Away while its workspace isn't shown (see Workspaces), or while the desktop is (see ShowDesktop)
    return LToplevelRole::rolePos() + SkIPoint::Make(0, titlebarHeight()) + Workspaces::ParkOffset(surface()) +
           ShowDesktop::ParkOffset(surface());
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

    // Landing in another workspace (e.g. the user is in a fullscreen window's): not activated, its
    // dock icon bounces instead (see Attention)
    if (!surface()->mapped() && Attention::WouldLeave(*this) && !Attention::IsUserLaunch(appId()))
    {
        configureState(pendingConfiguration().windowState & ~CZWinActivated);
        Attention::Request(*this);
    }

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

void ToplevelRole::unsetMaximizedRequest()
{
    if (fullscreenLocked())
        return;

    LToplevelRole::unsetMaximizedRequest();
}

void ToplevelRole::setMinimizedRequest()
{
    if (fullscreenLocked())
        return;

    LToplevelRole::setMinimizedRequest();
}

void ToplevelRole::setMaximizedRequest()
{
    if (fullscreenLocked())
        return;

    LToplevelRole::setMaximizedRequest();
    configureMaximizedSize();
}

void ToplevelRole::setFullscreenRequest(LOutput *preferredOutput)
{
    // Child toplevels (dialogs, etc.) stay with their parent: never fullscreen
    if (surface()->parent())
        return;

    LOutput *output { preferredOutput ? preferredOutput : cursor()->output() };

    if (pendingConfiguration().windowState.has(CZWinFullscreen) || !output)
        return;

    // Snapshot first (as it is), then into its own workspace
    FullscreenAnimation::Begin(*this, true, output);
    LToplevelRole::setFullscreenRequest(output);
    Workspaces::Sync(*this);
}

void ToplevelRole::unsetFullscreenRequest()
{
    leaveFullscreen(true);
}

void ToplevelRole::activateRequest()
{
    // An app asking to be activated in another workspace: its dock icon bounces instead, unless the
    // user just launched it from the dock (see Attention)
    if (Attention::ActivatingByToken() && Attention::WouldLeave(*this) && !Attention::ConsumeUserLaunch(appId()))
    {
        Attention::Request(*this);
        return;
    }

    // The windows moved aside come back
    ShowDesktop::WindowActivated(this);

    // Exposé closes onto it, like when it's clicked there (it activates and raises it)
    if (!Expose::LeaveFor(*this))
        LToplevelRole::activateRequest();
}

void ToplevelRole::keepReachable(LOutput *output) noexcept
{
    LSurface *surf { surface() };

    if (!surf || !output || moveSession().isActive() || resizeSession().isActive())
        return;

    const auto &state { pendingConfiguration().windowState };
    const SkIRect avail { SkIRect::MakePtSize(output->pos() + output->availableGeometry().topLeft(), output->availableGeometry().size()) };

    if (state.has(CZWinFullscreen))
    {
        setExclusiveOutput(output);
        surf->setPos(output->pos());
        configureSize(output->size());
        return;
    }

    if (state.has(CZWinMaximized))
    {
        setExclusiveOutput(output);
        surf->setPos(avail.topLeft());
        configureMaximizedSize();
        return;
    }

    // The size it's about to have if a configuration is on its way (e.g. leaving fullscreen while the
    // top bar comes back: its current size is still the fullscreen one, fitting that would replace
    // the size it's being restored to)
    const auto &pending { pendingConfiguration() };
    const bool awaiting { pending.serial != serial() && !pending.size.isEmpty() };
    const SkISize size { awaiting ? pending.size : windowGeometry().size() };

    if (size.isEmpty() || avail.isEmpty())
        return;

    // The whole window (title bar included) inside the available area, shrunk if needed
    const Int32 titlebar { pendingTitlebarHeight() };
    const SkISize fit { std::min(size.width(), avail.width()), std::max(1, std::min(size.height(), avail.height() - titlebar)) };

    if (fit != size)
        configureSize(fit);

    const SkIPoint pos {
        std::clamp(surf->pos().x(), avail.x(), std::max(avail.x(), avail.right() - fit.width())),
        std::clamp(surf->pos().y(), avail.y(), std::max(avail.y(), avail.bottom() - fit.height() - titlebar)) };

    if (pos != surf->pos())
        surf->setPos(pos);
}

void ToplevelRole::leaveFullscreen(bool animated) noexcept
{
    if (!pendingConfiguration().windowState.has(CZWinFullscreen))
        return;

    // Where it goes back to (prevRect, see LToplevelRole): inside the available area, title bar
    // included. E.g. an app that started fullscreen has none (or the whole area, which the title
    // bar would push past the bottom).
    if (LOutput *output { exclusiveOutput() ? exclusiveOutput() : cursor()->output() })
    {
        const SkIRect avail { SkIRect::MakePtSize(output->pos() + output->availableGeometry().topLeft(), output->availableGeometry().size()) };
        const Int32 titlebar { pendingConfiguration().decorationMode == ServerSide ? SSDTheme::TitlebarHeight : 0 };
        SkISize size { prevRect.size() };

        if (size.isEmpty())
            size = SkISize::Make(avail.width() * 2 / 3, avail.height() * 2 / 3);

        size = SkISize::Make(std::min(size.width(), avail.width()), std::max(1, std::min(size.height(), avail.height() - titlebar)));
        SkIPoint pos { prevRect.topLeft() };

        // Unknown (or not on this output): centered
        if (prevRect.isEmpty() || !SkIRect::Intersects(prevRect, avail))
            pos = SkIPoint::Make(avail.x() + (avail.width() - size.width()) / 2, avail.y() + (avail.height() - size.height() - titlebar) / 2);

        pos.fX = std::clamp(pos.x(), avail.x(), std::max(avail.x(), avail.right() - size.width()));
        pos.fY = std::clamp(pos.y(), avail.y(), std::max(avail.y(), avail.bottom() - size.height() - titlebar));
        prevRect = SkIRect::MakePtSize(pos, size);
    }

    if (animated)
        FullscreenAnimation::Begin(*this, false, exclusiveOutput() ? exclusiveOutput() : cursor()->output());

    // Back to maximized (if it was): with the title bar again.
    LToplevelRole::unsetFullscreenRequest();
    configureMaximizedSize();
    Workspaces::Sync(*this); // Back to the desktop
}

void ToplevelRole::restoredFromFullscreen() noexcept
{
    LSurface *surf { surface() };

    // Dragged meanwhile: where the user takes it
    if (!surf || isFullscreen() || isMaximized() || moveSession().isActive())
    {
        m_restoringFromFullscreen = false;
        return;
    }

    // The output it returns to: the one its previous rect is on (Louvre already cleared the
    // exclusive output; the cursor may be on another one)
    LOutput *output { nullptr };
    Int64 best { 0 };

    for (LOutput *o : compositor()->outputs())
    {
        SkIRect area;

        if (area.intersect(o->rect(), prevRect) && Int64(area.width()) * area.height() > best)
        {
            best = Int64(area.width()) * area.height();
            output = o;
        }
    }

    if (!output)
        output = cursor()->output();

    // Still at its fullscreen size (the state applied before its new size): at prevRect for now,
    // made reachable once it has the size asked for
    const SkISize size { windowGeometry().size() };

    if (output && size.width() >= output->size().width() && size.height() >= output->size().height() &&
        !(prevRect.width() >= output->size().width() && prevRect.height() >= output->size().height()))
    {
        surf->setPos(prevRect.topLeft());
        return;
    }

    m_restoringFromFullscreen = false;
    surf->setPos(prevRect.topLeft());

    // Its own size if it kept another one: reachable anyway
    if (output)
        keepReachable(output);
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

    // Fullscreen ones keep them, even client-side decorated ones: the title bar is revealed with the
    // top bars (see TopbarAutoHide)
    const bool wanted { decorationMode() == ServerSide || isFullscreen() };

    if (wanted && !m_ssd)
        m_ssd = std::make_unique<SSD>(*this);
    else if (!wanted && m_ssd)
        m_ssd.reset();
    else if (m_ssd)
        m_ssd->update();

    // Square corners while fullscreen
    surf->view.setBottomRadius(m_ssd && !isFullscreen() ? SSDTheme::BorderRadius : 0);

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
    // Like Louvre's default, but minimized windows stay activated: they're drawn as they were (e.g.
    // with their background blur) in their dock thumbnail. Restored without becoming the active
    // window (e.g. another window was activated meanwhile), they're deactivated then.
    if (isMinimized())
    {
        if (surface()->hasPointerFocus())
            seat()->pointer()->setFocus(nullptr);

        if (surface()->hasKeyboardFocus())
            seat()->keyboard()->setFocus(nullptr);

        moveSession().stop();
        resizeSession().stop();
    }
    else if (pendingConfiguration().windowState.has(CZWinActivated) && seat()->activeToplevel() != this)
        configureState(pendingConfiguration().windowState & ~CZWinActivated);

    auto *surf { static_cast<Surface*>(surface()) };

    // Not while Exposé is shown: right away (it rebuilds its layout)
    if (surf->mapped() && !Expose::Active())
        MinimizeAnimation::Start(*this, isMinimized());

    surf->view.syncVisibility();
    Workspaces::Sync(*this);
    Expose::WindowsChanged();

    // Restored: the windows moved aside come back
    if (!isMinimized())
        ShowDesktop::WindowActivated(this);
    TopbarAutoHide::UpdateAll();
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

    // (Un)fullscreened: its workspace, and the frame of its new size for the animation
    if (changes.has(StateChanged) && isFullscreen() != prev.windowState.has(CZWinFullscreen))
    {
        FullscreenAnimation::StateApplied(*this);
        Expose::WindowsChanged(); // Into/out of the desktop's layout
        static_cast<Surface*>(surface())->view.syncOpaqueRegion(); // Fully opaque while fullscreen

        // Out of fullscreen: back where it was (see restoredFromFullscreen())
        if (!isFullscreen())
        {
            m_restoringFromFullscreen = true;
            restoredFromFullscreen();
        }
        else
            m_restoringFromFullscreen = false;
    }
    else if (m_restoringFromFullscreen && changes.has(WindowGeometryChanged))
        restoredFromFullscreen();

    Workspaces::Sync(*this);

    // Activated in another workspace (e.g. from the dock): show it. Not mapped yet (Louvre applies
    // the state before mapping): once mapped and placed, see Surface::mappingChanged()
    if (changes.has(StateChanged) && isActivated() && !prev.windowState.has(CZWinActivated) && surface()->mapped())
        Workspaces::Reveal(*this);

    TopbarAutoHide::UpdateAll(); // (Un)fullscreened: hide/show the top bars of its output

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

        if (!keyboard->grab() && !surface()->hasKeyboardFocus() && !SessionLockManager::IsLocked())
            keyboard->setFocus(surface());
    }
}

void ToplevelRole::imageCaptureFrameRequest(LImageCaptureFrame *frame)
{
    // Like Louvre's default (the surface tree scaled to the buffer), plus the background blur behind
    // each surface (as last rendered on screen) and, for dock thumbnails (see CaptureManager), a
    // title bar.
    const bool thumbnail { CaptureManager::IsThumbnail(frame->session()) };
    auto target { frame->surface() };

    if (!target)
    {
        frame->fail(LImageCaptureFrame::Unknown);
        return;
    }

    const SkISize bufferSize { frame->session().bufferSize() };
    const SkRegion region { thumbnail ? SkRegion(SkIRect::MakeSize(bufferSize)) : frame->paintRegion() }; // Small: thumbnails are always redrawn whole

    if (region.isEmpty())
    {
        frame->ready();
        return;
    }

    // The window (title bar included for thumbnails) in logical units, scaled to the buffer
    SkISize window { windowGeometry().isEmpty() ? surface()->size() : windowGeometry().size() };
    const Int32 titlebar { thumbnail ? titlebarHeight() : 0 };
    window.fHeight += titlebar;

    const SkScalar sx { SkScalar(bufferSize.width()) / SkScalar(std::max(1, window.width())) };
    const SkScalar sy { SkScalar(bufferSize.height()) / SkScalar(std::max(1, window.height())) };

    auto pass { target->beginPass(RPassCap_Painter | RPassCap_SkCanvas) };

    if (!pass)
    {
        frame->fail(LImageCaptureFrame::Unknown);
        return;
    }

    // Transparent where nothing is drawn
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

    // Surface coordinates relative to the window geometry (below the title bar), to buffer pixels
    const SkIPoint origin { -windowGeometry().x(), -windowGeometry().y() + titlebar };

    const std::function<void(LSurface*, SkIPoint)> draw = [&](LSurface *s, SkIPoint pos)
    {
        // Its background blur, behind everything it shows
        if (auto *blur { static_cast<SurfaceBlurManager*>(s->backgroundBlur()) }; blur && blur->visible())
        {
            SkCanvas &c { *pass->getCanvas() };
            c.save();
            c.clipRegion(region);
            c.translate(pos.x() * sx, pos.y() * sy);
            c.scale(sx, sy);
            blur->fx.drawBlurred(c);
            c.restore();
            painter = pass->getPainter();
            painter->setBlendMode(RBlendMode::SrcOver);
        }

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
