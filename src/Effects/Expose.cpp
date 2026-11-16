#include <Effects/Expose.h>
#include <Effects/Snapshot.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Roles/ToplevelRole.h>
#include <Decorations/SSDTheme.h>
#include <Roles/Surface.h>
#include <Roles/LayerRole.h>
#include <Scene/Scene.h>
#include <Seat/Output.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Roles/LPopupRole.h>
#include <Core/CZTimer.h>
#include <Core/CZEase.h>
#include <Core/CZBus.h>
#include <Effects/SurfaceBlurManager.h>
#include <systemd/sd-bus.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Ream/RPainter.h>
#include <CZ/skia/core/SkCanvas.h>
#include <unordered_map>
#include <CZ/skia/core/SkMaskFilter.h>
#include <CZ/skia/core/SkBlurTypes.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <cmath>

static std::unique_ptr<Expose> s_expose;

Expose *Expose::Get(bool create) noexcept
{
    if (!s_expose && create)
        s_expose.reset(new Expose());

    return s_expose.get();
}

void Expose::Clear() noexcept
{
    s_expose.reset();
}

Expose::Expose() noexcept :
    m_spring(0.0, 0.0, 0.0, SpringStiffness, Workspaces::SpringDamping,
        [this](CZAnimation *a)
        {
            m_progress = a->value();
            apply();
        },
        [this](CZAnimation*)
        {
            m_progress = m_spring.to();
            apply();

            // Closed: the windows are shown again
            if (!m_open && !m_swiping && m_progress <= 0.001 && !m_zoomThumb)
                deactivate();
        }),
    m_addAnim(AddMs,
        [this](CZAnimation *a){ addStep(a->value()); },
        [this](CZAnimation*)
        {
            // It got there: the bar is rebuilt with it (next frame, see ProcessPending())
            addStep(1.0);
            m_addScreen = nullptr;
            m_rebuildPending = true;
            compositor()->repaintAllOutputs();
        }),
    m_zoom(0.0, 1.0, 0.0, SpringStiffness, Workspaces::SpringDamping,
        [this](CZAnimation *a){ zoomStep(a->value()); },
        [this](CZAnimation*)
        {
            // It fills the screen just like the workspace: shown instead (see zoomInto())
            zoomStep(1.0);

            if (m_zoomScreen)
                if (Workspaces *ws { m_zoomScreen->output->workspaces.get() })
                    ws->showCurrent();

            if (m_zoomThumb)
                m_zoomThumb->box.setVisible(false);

            m_zoomThumb = nullptr;
            m_zoomScreen = nullptr;
            m_progress = 0.0;
            deactivate();
        })
{}

Expose::~Expose()
{
    m_spring.pause();
    m_zoom.pause();
}

bool Expose::Active() noexcept
{
    return s_expose && s_expose->m_active;
}

Output *Expose::ShownOn() noexcept
{
    return Active() ? s_expose->m_output : nullptr;
}

/*
 * The top bars leave (slide up and fade out) within the first TopbarHideEnd of Exposé's progress,
 * well before the workspace bar is fully in: otherwise they'd show blurred through it. Closing,
 * they come back in the last part.
 */
static constexpr Float64 TopbarHideEnd { 0.2 };

static Float64 TopbarHideProgress(Float64 progress) noexcept
{
    return std::min(1.0, std::clamp(progress, 0.0, 1.0) / TopbarHideEnd);
}

Int32 Expose::TopbarOffset(const LLayerRole &layerRole) noexcept
{
    if (!Active() || !TopbarAutoHide::IsTopbar(layerRole) || layerRole.exclusiveOutput() != s_expose->m_output)
        return 0;

    const LSurface *s { layerRole.surface() };
    const Float64 p { TopbarHideProgress(s_expose->m_progress) };
    return Int32(std::lround(p * (s->size().height() + std::max(0, layerRole.margins().top))));
}

bool Expose::HidesSurface(const LSurface *surface) noexcept
{
    if (!Active() || !surface)
        return false;

    const LSurface *root { surface->parent() ? surface->topmostParent() : surface };
    const LToplevelRole *toplevel { root->toplevel() };

    if (!toplevel)
        return false;

    // Zooming into a workspace: it's current already, its windows hidden until the thumbnail fills
    // the screen. The one that was shown stays in the background meanwhile (e.g. a fullscreen
    // window, shown as is).
    if (s_expose->m_zoomScreen && s_expose->m_zoomThumb)
    {
        size_t index;

        if (Workspaces *ws { Workspaces::Find(root, &index) }; ws && &ws->output() == s_expose->m_zoomScreen->output &&
            index == s_expose->m_zoomThumb->workspace)
            return true;
    }

    for (const auto &w : s_expose->m_windows)
        if (w->laidOut && w->toplevel.get() == toplevel)
            return true;

    return false;
}

/* ---------------------------------------- Building ---------------------------------------- */

// The window as shown (title bar included), where it'd be if its workspace were shown.
static SkRect WindowRect(ToplevelRole &toplevel) noexcept
{
    const LSurface *s { toplevel.surface() };
    const Int32 titlebar { toplevel.titlebarHeight() };
    const SkIPoint pos { s->rolePos() - Workspaces::ParkOffset(s) };
    return SkRect::Make(SkIRect::MakeXYWH(pos.x(), pos.y() - titlebar, s->size().width(), s->size().height() + titlebar));
}

static SkRect Lerp(const SkRect &a, const SkRect &b, SkScalar t) noexcept
{
    return SkRect::MakeLTRB(a.left() + (b.left() - a.left()) * t, a.top() + (b.top() - a.top()) * t,
                            a.right() + (b.right() - a.right()) * t, a.bottom() + (b.bottom() - a.bottom()) * t);
}

static LSurface *DeskOf(const LOutput *output) noexcept
{
    for (LSurface *s : compositor()->surfaces())
        if (!s->parent() && s->mapped() && Crystals::IsDesk(s) && s->layerRole()->exclusiveOutput() == output)
            return s;

    return nullptr;
}

void Expose::snapshot(Window &w) noexcept
{
    w.dirty = false;

    if (!w.toplevel || !w.output)
        return;

    const Int32 scale { std::max(1, Int32(std::ceil(w.output->scale()))) };
    auto image { Snapshot::Toplevel(*w.toplevel.get(), scale, w.margins) };

    if (!image)
        return;

    // Swapping images doesn't damage the nodes (AKImage leaves that to the owner): redrawn whole
    w.node.setImage(image);
    w.thumb.setImage(image);
    w.node.addDamage(AK_IRECT_INF);
    w.thumb.addDamage(AK_IRECT_INF);
}

static std::shared_ptr<RImage> CloseShadowImage(std::unordered_map<Int32, std::shared_ptr<RImage>> &cache, Int32 scale) noexcept;

void Expose::build() noexcept
{
    // Thumbnails are recreated: a press in progress is dropped, an addition finished
    m_addAnim.pause();
    m_addScreen = nullptr;
    m_addMoves.clear();
    m_pressScreen = nullptr;
    m_pressThumb = nullptr;
    m_dragging = false;

    m_zoom.pause();
    m_zoomThumb = nullptr;
    m_zoomScreen = nullptr;
    m_windows.clear();
    m_screens.clear();

    AKContainer &layer { GetScene()->expose };

    // Below everything else of Exposé (see m_windowsClip)
    const SkRegion noClipInput;
    m_windowsClip.setInputRegion(&noClipInput);
    m_windowsLayer.setInputRegion(&noClipInput);
    m_windowsClip.layout().setPositionType(YGPositionTypeAbsolute);
    m_windowsLayer.layout().setPositionType(YGPositionTypeAbsolute);
    m_windowsClip.enableChildrenClipping(true);
    m_windowsLayer.enableChildrenClipping(false);

    if (layer.children(true).empty())
        m_windowsClip.setParent(&layer);
    else if (layer.children(true).front() != &m_windowsClip)
        m_windowsClip.insertBefore(layer.children(true).front());

    placeWindowsClip();

    // Every output's workspaces (created on first use: otherwise no window would be found in them)
    for (LOutput *o : compositor()->outputs())
        Workspaces::Of(o);

    // The windows, bottom to top
    for (LSurfaceLayer l : { LLayerBottom, LLayerMiddle, LLayerTop })
    {
        for (LSurface *s : compositor()->layers()[l])
        {
            auto *toplevel { static_cast<ToplevelRole*>(s->toplevel()) };

            if (!toplevel || s->parent() || !s->mapped() || toplevel->isMinimized() || s->size().isEmpty())
                continue;

            size_t index;
            Workspaces *ws { Workspaces::Find(s, &index) };

            if (!ws || &ws->output() != m_output)
                continue;

            auto &w { m_windows.emplace_back(std::make_unique<Window>()) };
            w->toplevel.reset(toplevel);
            w->output = &ws->output();
            w->workspace = index;
            // The desktop's windows are laid out; fullscreen workspaces are shown as they are
            w->laidOut = ws->isDesktop(index);
            w->from = WindowRect(*toplevel);

            // The window itself: its title bar and content (server-side), or its geometry (the
            // client's decorations, without their shadows)
            const SkIRect &geo { toplevel->windowGeometry() };
            const Int32 titlebar { toplevel->titlebarHeight() };

            if (titlebar > 0 || geo.isEmpty())
                w->geometry = SkRect::MakeWH(w->from.width(), w->from.height());
            else
                w->geometry = SkRect::Make(geo);

            const SkRegion empty;

            for (AKImage *node : { &w->node, &w->thumb })
            {
                node->setInputRegion(&empty);
                node->enableAutoDamage(true);
                node->layout().setPositionType(YGPositionTypeAbsolute);
            }

            if (w->laidOut)
                w->node.setParent(&m_windowsLayer);

            snapshot(*w);
        }
    }

    // The hovered window's outline, above the windows (clipped with them: hidden while dragging)
    m_hoverWindow = nullptr;
    m_pressWindow = nullptr;
    m_windowDragging = false;
    m_dropThumb = nullptr;
    const SkRegion noInput;
    m_windowOutline.setInputRegion(&noInput);
    m_windowOutline.setThickness(4);
    m_windowOutline.layout().setPositionType(YGPositionTypeAbsolute);
    m_windowOutline.setVisible(false);
    m_windowOutline.setParent(&m_windowsLayer);

    // The hovered window's close button (like the thumbnails')
    const bool dark { GetCompositor()->colorScheme() == CZColorScheme::Dark };
    m_windowCloseShadow.setInputRegion(&noInput);
    m_windowCloseShadow.setImage(CloseShadowImage(m_closeShadows, std::max(1, Int32(std::ceil(m_output ? m_output->scale() : 1.f)))));
    m_windowCloseShadow.layout().setPositionType(YGPositionTypeAbsolute);
    m_windowCloseShadow.layout().setWidth(CloseSize + 2 * CloseShadowPad);
    m_windowCloseShadow.layout().setHeight(CloseSize + 2 * CloseShadowPad);
    m_windowCloseShadow.setVisible(false);
    m_windowCloseShadow.setParent(&m_windowsLayer);
    m_windowClose.setBackgroundColor(dark ? 0xFF5A5A5E : 0xFFF2F2F7);
    m_windowClose.setInputRegion(&noInput);
    m_windowClose.layout().setPositionType(YGPositionTypeAbsolute);
    m_windowClose.layout().setWidth(CloseSize);
    m_windowClose.layout().setHeight(CloseSize);
    m_windowClose.layout().setJustifyContent(YGJustifyCenter);
    m_windowClose.layout().setAlignItems(YGAlignCenter);
    m_windowCloseIcon.setInputRegion(&noInput);
    m_windowCloseIcon.setColor(dark ? 0xFFEBEBF0 : 0xFF48484A);
    m_windowClose.setVisible(false);
    m_windowClose.setParent(&m_windowsLayer);

    // Its bar (above the windows)
    if (m_output)
    {
        auto &screen { m_screens.emplace_back(std::make_unique<Screen>()) };
        screen->output = m_output;
        layoutScreen(*screen);
    }

    // Removed workspaces' thumbnails still fading out: above the new bars
    for (auto &r : m_removed)
        if (!r->done)
            r->box.setParent(&layer);
}

// Windows laid out in rows, keeping their order (top to bottom, left to right), as large as possible
// (never larger than they are) without overlapping. The row count covering the most area wins.
static void LayoutSlots(const std::vector<SkRect> &from, std::vector<SkRect> &slots, const SkRect &area, SkScalar gap) noexcept
{
    const size_t n { from.size() };
    slots.assign(n, SkRect::MakeEmpty());

    if (n == 0 || area.isEmpty())
        return;

    // By their centers: top to bottom (rows), then left to right
    std::vector<size_t> order(n);

    for (size_t i = 0; i < n; i++)
        order[i] = i;

    std::sort(order.begin(), order.end(), [&from](size_t a, size_t b){ return from[a].centerY() < from[b].centerY(); });

    SkScalar bestScore { -1.f };
    size_t bestRows { 1 };

    const auto rowsOf = [&](size_t rows, std::vector<std::vector<size_t>> &out)
    {
        const size_t cols { (n + rows - 1) / rows };
        out.assign(rows, {});

        for (size_t i = 0; i < n; i++)
            out[std::min(rows - 1, i / cols)].push_back(order[i]);

        for (auto &row : out)
            std::sort(row.begin(), row.end(), [&from](size_t a, size_t b){ return from[a].centerX() < from[b].centerX(); });

        return cols;
    };

    std::vector<std::vector<size_t>> rows;

    for (size_t r = 1; r <= n; r++)
    {
        const size_t cols { rowsOf(r, rows) };
        const SkScalar cellW { area.width() / cols }, cellH { area.height() / r };
        SkScalar score { 0.f }, minScale { 1.f };

        for (const auto &row : rows)
            for (size_t i : row)
            {
                const SkScalar s { std::min({ 1.f, (cellW - gap) / from[i].width(), (cellH - gap) / from[i].height() }) };
                minScale = std::min(minScale, s);
                score += s * s * from[i].width() * from[i].height();
            }

        // Tiny windows don't count as well (they'd be hard to tell apart)
        score *= 0.5f + 0.5f * minScale;

        if (score > bestScore)
        {
            bestScore = score;
            bestRows = r;
        }
    }

    const size_t cols { rowsOf(bestRows, rows) };
    const SkScalar cellW { area.width() / cols }, cellH { area.height() / bestRows };

    for (size_t r = 0; r < rows.size(); r++)
    {
        // Rows with fewer windows centered
        const SkScalar x0 { area.x() + (area.width() - cellW * rows[r].size()) * 0.5f };

        for (size_t c = 0; c < rows[r].size(); c++)
        {
            const size_t i { rows[r][c] };
            const SkScalar s { std::max(0.05f, std::min({ 1.f, (cellW - gap) / from[i].width(), (cellH - gap) / from[i].height() })) };
            const SkScalar w { from[i].width() * s }, h { from[i].height() * s };
            const SkScalar cx { x0 + cellW * (c + 0.5f) }, cy { area.y() + cellH * (r + 0.5f) };
            slots[i] = SkRect::MakeXYWH(std::round(cx - w * 0.5f), std::round(cy - h * 0.5f), std::round(w), std::round(h));
        }
    }
}

// A soft dark circle (the close button's shadow), blurred within CloseShadowPad around it.
static std::shared_ptr<RImage> CloseShadowImage(std::unordered_map<Int32, std::shared_ptr<RImage>> &cache, Int32 scale) noexcept
{
    if (auto it { cache.find(scale) }; it != cache.end())
        return it->second;

    const Int32 size { Expose::CloseSize + 2 * Expose::CloseShadowPad };
    auto surface { RSurface::Make(SkISize::Make(size, size), scale, true) };

    if (!surface)
        return nullptr;

    if (auto pass { surface->beginPass(RPassCap_SkCanvas) })
    {
        auto &c { *pass->getCanvas() };
        c.clear(SK_ColorTRANSPARENT);
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(0x73000000);
        paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, Expose::CloseShadowPad / 3.f));
        c.drawCircle(SkPoint::Make(size / 2.f, size / 2.f), Expose::CloseSize / 2.f, paint);
    }

    return cache[scale] = surface->image();
}

void Expose::layoutScreen(Screen &screen) noexcept
{
    Output &output { *screen.output };
    Workspaces *ws { Workspaces::Of(&output) };
    const SkRect r { SkRect::Make(output.rect()) };
    const CZColorScheme scheme { GetCompositor()->colorScheme() };
    const bool dark { scheme == CZColorScheme::Dark };

    // The bar: a thumbnail of each workspace, centered
    const SkScalar thumbH { std::round(r.height() * ThumbHeight) };
    const SkScalar thumbW { std::round(thumbH * r.width() / std::max(1.f, r.height())) };
    const size_t count { ws ? ws->count() : 1 };
    screen.barHeight = thumbH + 2 * BarPadding;

    const SkRegion empty;
    screen.bar.setInputRegion(&empty);
    screen.bar.layout().setPositionType(YGPositionTypeAbsolute);
    screen.bar.layout().setWidth(r.width());
    screen.bar.layout().setHeight(screen.barHeight);
    screen.bar.setParent(&GetScene()->expose);

    // It takes the top bar's place: the same material as Firmament's (Marco's MBlurMaterial::MenuBar),
    // all of it from the blur (the fill stays transparent, it only holds the effect)
    screen.barFill.setInputRegion(&empty);
    screen.barFill.setColor(CZAdaptiveColor(0));
    screen.barFill.layout().setPositionType(YGPositionTypeAbsolute);
    screen.barFill.layout().setWidthPercent(100.f);
    screen.barFill.layout().setHeightPercent(100.f);
    screen.barFill.setParent(&screen.bar);
    screen.blur.setFullSize();
    screen.blur.setColorScheme(scheme);
    screen.blur.setBlurRadius(BarBlurRadius);
    screen.blur.setMaterial(CZColorScheme::Light, { BarBlurSaturation, SkColor4f::FromColor(BarBlurTintLight) });
    screen.blur.setMaterial(CZColorScheme::Dark,  { BarBlurSaturation, SkColor4f::FromColor(BarBlurTintDark) });
    screen.barFill.addBackgroundEffect(&screen.blur);
    screen.shadow.setInputRegion(&empty);
    screen.shadow.layout().setHeight(BarShadow);
    screen.shadow.layout().setPosition(YGEdgeBottom, -BarShadow);
    screen.shadow.setOpacity(BarShadowOpacity);
    screen.shadow.setParent(&screen.bar);

    // Workspaces being removed (left by their window) take no room
    size_t shownCount { 0 };

    for (size_t i = 0; i < count; i++)
        if (!ws || !ws->closing(i))
            shownCount++;

    const SkScalar total { shownCount * thumbW + (shownCount > 0 ? shownCount - 1 : 0) * ThumbGap };
    const SkScalar x0 { std::round((r.width() - total) * 0.5f) };
    const SkScalar fit { thumbW / std::max(1.f, r.width()) }; // Output -> thumbnail

    // The desktop picture
    std::shared_ptr<RImage> desktop;

    if (LSurface *desk { DeskOf(&output) })
        desktop = Snapshot::View(*static_cast<Surface*>(desk), desk->size(), SkIPoint::Make(0, 0), 1);

    size_t slot { 0 };
    screen.slots.clear();
    screen.thumbWidth = thumbW;

    for (size_t i = 0; i < shownCount; i++)
        screen.slots.push_back(x0 + i * (thumbW + ThumbGap));

    for (size_t i = 0; i < count; i++)
    {
        auto &thumb { screen.thumbs.emplace_back(std::make_unique<Thumb>()) };
        thumb->workspace = i;
        const bool closing { ws && ws->closing(i) };
        thumb->rect = closing ? SkRect::MakeEmpty() : SkRect::MakeXYWH(x0 + slot++ * (thumbW + ThumbGap), BarPadding, thumbW, thumbH);
        thumb->box.setVisible(!closing);

        // The current one's outline, around it
        const Int32 out { thumb->frame.thickness() + ThumbOutlineGap };
        thumb->frame.setRadius(ThumbOutlineGap + 2);
        thumb->frame.layout().setPositionType(YGPositionTypeAbsolute);
        thumb->frame.layout().setPosition(YGEdgeLeft, thumb->rect.x() - out);
        thumb->frame.layout().setPosition(YGEdgeTop, thumb->rect.y() - out);
        thumb->frame.layout().setWidth(thumbW + 2 * out);
        thumb->frame.layout().setHeight(thumbH + 2 * out);
        thumb->frame.setVisible(ws && i == ws->current());
        thumb->frame.setParent(&screen.bar);

        thumb->box.setInputRegion(&empty);
        thumb->box.enableChildrenClipping(true);
        thumb->box.layout().setPositionType(YGPositionTypeAbsolute);
        thumb->box.layout().setPosition(YGEdgeLeft, thumb->rect.x());
        thumb->box.layout().setPosition(YGEdgeTop, thumb->rect.y());
        thumb->box.layout().setWidth(thumbW);
        thumb->box.layout().setHeight(thumbH);
        thumb->box.setParent(&screen.bar);

        for (AKNode *node : std::initializer_list<AKNode*>{ &thumb->fill, &thumb->desktop })
        {
            node->setInputRegion(&empty);
            node->layout().setPositionType(YGPositionTypeAbsolute);
            node->layout().setWidthPercent(100.f);
            node->layout().setHeightPercent(100.f);
            node->setParent(&thumb->box);
        }

        const bool isDesktop { ws && ws->isDesktop(i) };
        thumb->desktop.setImage(isDesktop ? desktop : nullptr);
        thumb->desktop.setVisible(isDesktop && desktop);
        thumb->topbar.setInputRegion(&empty);
        thumb->topbar.layout().setPositionType(YGPositionTypeAbsolute);
        thumb->topbar.setVisible(false);
    }

    // The close button, above the thumbnails (see setHovered())
    screen.closeButton.setBackgroundColor(dark ? 0xFF5A5A5E : 0xFFF2F2F7);
    screen.closeButton.layout().setPositionType(YGPositionTypeAbsolute);
    screen.closeButton.layout().setWidth(CloseSize);
    screen.closeButton.layout().setHeight(CloseSize);
    screen.closeButton.layout().setJustifyContent(YGJustifyCenter);
    screen.closeButton.layout().setAlignItems(YGAlignCenter);
    screen.closeButton.setInputRegion(&empty);
    screen.closeIcon.setInputRegion(&empty);
    screen.closeIcon.setColor(dark ? 0xFFEBEBF0 : 0xFF48484A);
    screen.closeButton.setVisible(false);
    screen.closeShadow.setInputRegion(&empty);
    screen.closeShadow.setImage(CloseShadowImage(m_closeShadows, std::max(1, Int32(std::ceil(output.scale())))));
    screen.closeShadow.layout().setPositionType(YGPositionTypeAbsolute);
    screen.closeShadow.layout().setWidth(CloseSize + 2 * CloseShadowPad);
    screen.closeShadow.layout().setHeight(CloseSize + 2 * CloseShadowPad);
    screen.closeShadow.setVisible(false);
    screen.closeShadow.setParent(&screen.bar);
    screen.closeButton.setParent(&screen.bar);

    // The plus button on the right, centered on the thumbnails, and the new desktop peeking in
    // from the edge behind it (shown while hovered, see setPlusHovered())
    screen.thumbHeight = thumbH;
    screen.desktopImage = desktop;
    screen.plusRect = SkRect::MakeXYWH(r.width() - BarPadding - PlusIconSize, BarPadding + (thumbH - PlusIconSize) / 2.f, PlusIconSize, PlusIconSize);
    screen.peekRect = SkRect::MakeXYWH(r.width() - thumbW * PeekVisible, BarPadding, thumbW, thumbH);

    screen.peek.setInputRegion(&empty);
    screen.peek.enableChildrenClipping(true);
    screen.peek.layout().setPositionType(YGPositionTypeAbsolute);
    screen.peek.setVisible(false);
    screen.peek.setParent(&screen.bar);

    for (AKImage *img : { &screen.peekDesktop, &screen.peekTopbar })
    {
        img->setInputRegion(&empty);
        img->layout().setPositionType(YGPositionTypeAbsolute);
        img->setParent(&screen.peek);
    }

    screen.peekDesktop.layout().setWidthPercent(100.f);
    screen.peekDesktop.layout().setHeightPercent(100.f);
    screen.peekDesktop.setImage(desktop);

    screen.plusShadow.setInputRegion(&empty);
    screen.plusShadow.setImage(CloseShadowImage(m_closeShadows, std::max(1, Int32(std::ceil(output.scale())))));
    screen.plusShadow.layout().setPositionType(YGPositionTypeAbsolute);
    screen.plusShadow.layout().setPosition(YGEdgeLeft, screen.plusRect.x() - CloseShadowPad);
    screen.plusShadow.layout().setPosition(YGEdgeTop, screen.plusRect.y() - CloseShadowPad + 2);
    screen.plusShadow.layout().setWidth(CloseSize + 2 * CloseShadowPad);
    screen.plusShadow.layout().setHeight(CloseSize + 2 * CloseShadowPad);
    screen.plusShadow.setVisible(false); // Just the sign (no circle)
    screen.plusShadow.setParent(&screen.bar);

    screen.plusButton.setBackgroundColor(SK_ColorTRANSPARENT);
    screen.plusButton.setInputRegion(&empty);
    screen.plusButton.layout().setPositionType(YGPositionTypeAbsolute);
    screen.plusButton.layout().setPosition(YGEdgeLeft, screen.plusRect.x());
    screen.plusButton.layout().setPosition(YGEdgeTop, screen.plusRect.y());
    screen.plusButton.layout().setWidth(PlusIconSize);
    screen.plusButton.layout().setHeight(PlusIconSize);
    screen.plusButton.layout().setJustifyContent(YGJustifyCenter);
    screen.plusButton.layout().setAlignItems(YGAlignCenter);
    screen.plusIcon.setInputRegion(&empty);
    screen.plusIcon.setColor(dark ? 0xFFEBEBF0 : 0xFF48484A);
    screen.plusButton.setParent(&screen.bar);

    // The top bar in the desktop's thumbnail (it's only shown there), as is (not faded), with a
    // blur over the thumbnail's picture like the real one's
    if (!screen.thumbs.empty())
    {
        for (Surface *s : GetSurfaces())
        {
            if (s->parent() || !s->mapped() || !s->layerRole() || s->layerRole()->exclusiveOutput() != &output ||
                !TopbarAutoHide::IsTopbar(*s->layerRole()))
                continue;

            const SkScalar opacity { s->view.view.opacity() };
            s->view.view.setOpacity(1.f);
            auto image { Snapshot::View(*s, s->size(), SkIPoint::Make(0, 0),
                                        std::max(1, Int32(std::ceil(output.scale())))) };
            s->view.view.setOpacity(opacity);

            if (!image)
                break;

            // Where it is on the desktop (not slid away by a fullscreen workspace)
            const SkIPoint pos { static_cast<LayerRole*>(s->layerRole())->baseRolePos() };
            screen.topbarImage = image;
            screen.topbarFrom = SkRect::Make(SkIRect::MakePtSize(pos, s->size()));

            for (auto &desk : screen.thumbs)
            {
                if (!ws || !ws->isDesktop(desk->workspace))
                    continue;

                desk->topbarFrom = screen.topbarFrom;
                desk->topbar.setImage(image);
                desk->topbar.setVisible(true);
                desk->topbar.setParent(&desk->box);
            }
            break;
        }
    }

    // The windows in their workspace's thumbnail (bottom to top), and the ones shown laid out
    std::vector<Window*> shown;
    std::vector<SkRect> from, slots;

    for (auto &w : m_windows)
    {
        if (w->output != &output)
            continue;

        if (w->workspace < screen.thumbs.size())
        {
            Thumb &thumb { *screen.thumbs[w->workspace] };

            // Below the top bar
            if (thumb.topbar.parent() == &thumb.box)
                w->thumb.insertBefore(&thumb.topbar);
            else
                w->thumb.setParent(&thumb.box);
        }

    }

    for (auto &thumb : screen.thumbs)
        placeThumbWindows(screen, *thumb, fit);

    // Below the bar, above the dock (its zone, or where it shows if it auto-hides: its input region
    // spans the dock and below), away from the edges
    const SkIRect &avail { output.availableGeometry() };
    SkScalar bottom { r.y() + avail.bottom() };

    for (LSurface *s : compositor()->surfaces())
        if (!s->parent() && s->mapped() && s->layerRole() && s->layerRole()->exclusiveOutput() == &output &&
            s->layerRole()->scope() == Crystals::ShellScopes[0] && !s->inputRegion().isEmpty())
            bottom = std::min(bottom, r.bottom() - SkScalar(s->inputRegion().getBounds().height()));

    const SkRect area { SkRect::MakeLTRB(r.x() + WindowGap, r.y() + screen.barHeight + WindowGap, r.right() - WindowGap, bottom - WindowGap) };
    // Each workspace laid out on its own (they slide, see apply())
    for (size_t space = 0; space < screen.thumbs.size(); space++)
    {
        shown.clear();
        from.clear();

        for (auto &w : m_windows)
            if (w->output == &output && w->workspace == space && w->laidOut)
            {
                shown.push_back(w.get());
                from.push_back(w->from);
            }

        LayoutSlots(from, slots, area, WindowGap);

        for (size_t i = 0; i < shown.size(); i++)
            shown[i]->slot = slots[i];
    }
}

void Expose::placeThumbWindows(Screen &screen, Thumb &thumb, SkScalar fit) noexcept
{
    const SkRect r { SkRect::Make(screen.output->rect()) };

    if (thumb.topbar.parent() == &thumb.box)
    {
        const SkRect &f { thumb.topbarFrom };
        thumb.topbar.layout().setPosition(YGEdgeLeft, (f.x() - r.x()) * fit);
        thumb.topbar.layout().setPosition(YGEdgeTop, (f.y() - r.y()) * fit);
        thumb.topbar.layout().setWidth(std::max(1.f, f.width() * fit));
        thumb.topbar.layout().setHeight(std::max(1.f, f.height() * fit));
    }

    for (auto &w : m_windows)
    {
        if (w->output != screen.output || w->workspace != thumb.workspace)
            continue;

        // Where it is on the output, scaled (its shadow too)
        const SkRect &f { w->from };
        const SkRect in { (f.x() - r.x()) * fit, (f.y() - r.y()) * fit, (f.right() - r.x()) * fit, (f.bottom() - r.y()) * fit };
        w->thumb.layout().setPosition(YGEdgeLeft, in.x() - w->margins.left() * fit);
        w->thumb.layout().setPosition(YGEdgeTop, in.y() - w->margins.top() * fit);
        w->thumb.layout().setWidth(std::max(1.f, in.width() + (w->margins.left() + w->margins.right()) * fit));
        w->thumb.layout().setHeight(std::max(1.f, in.height() + (w->margins.top() + w->margins.bottom()) * fit));
    }
}

void Expose::zoomInto(Screen &screen, Thumb &thumb) noexcept
{
    // From where it is in the bar, above everything of Exposé
    const SkIRect &r { screen.output->rect() };
    const SkScalar p { SkScalar(std::clamp(m_progress, 0.0, 1.0)) };
    m_zoomFrom = thumb.rect.makeOffset(r.x(), r.y() - screen.barHeight * (1.f - p));
    m_zoomScreen = &screen;
    m_zoomThumb = &thumb;
    m_open = false;
    notify(false);
    m_spring.pause();

    thumb.frame.setVisible(false);
    thumb.box.setParent(&GetScene()->expose);

    // Switched to right away (activation, top bar...), but the workspace that was shown stays in the
    // background until the thumbnail fills the screen (its windows hidden, see HidesSurface())
    if (Workspaces *ws { Workspaces::Of(screen.output) })
        ws->jumpTo(Int64(thumb.workspace), false);

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    zoomStep(0.0);
    m_zoom.setStiffness(ZoomStiffness);
    m_zoom.setFrom(0.0);
    m_zoom.setTo(1.0);
    m_zoom.setVelocity(0.0);
    m_zoom.reset();
}

void Expose::zoomStep(Float64 t) noexcept
{
    if (!m_zoomThumb || !m_zoomScreen)
        return;

    const SkRect to { SkRect::Make(m_zoomScreen->output->rect()) };
    const SkRect rect { Lerp(m_zoomFrom, to, SkScalar(t)) };
    Thumb &thumb { *m_zoomThumb };
    thumb.box.layout().setPosition(YGEdgeLeft, rect.x());
    thumb.box.layout().setPosition(YGEdgeTop, rect.y());
    thumb.box.layout().setWidth(std::max(1.f, rect.width()));
    thumb.box.layout().setHeight(std::max(1.f, rect.height()));
    placeThumbWindows(*m_zoomScreen, thumb, rect.width() / std::max(1.f, to.width()));
    compositor()->repaintAllOutputs();
}

void Expose::placeWindowsClip() noexcept
{
    const SkIRect r { m_output ? m_output->rect() : SkIRect::MakeEmpty() };
    m_windowsClip.layout().setPosition(YGEdgeLeft, r.x());
    m_windowsClip.layout().setPosition(YGEdgeTop, r.y());
    m_windowsClip.layout().setWidth(r.width());
    m_windowsClip.layout().setHeight(r.height());
    m_windowsLayer.layout().setPosition(YGEdgeLeft, -r.x());
    m_windowsLayer.layout().setPosition(YGEdgeTop, -r.y());
}

void Expose::restackWindows() noexcept
{
    // Back in the clipped layer, bottom to top
    for (auto &w : m_windows)
    {
        if (!w->laidOut)
            continue;

        w->node.setParent(nullptr);
        w->node.setParent(&m_windowsLayer);
    }

    // The hovered window's outline and close button above them
    for (AKNode *node : std::initializer_list<AKNode*>{ &m_windowOutline, &m_windowCloseShadow, &m_windowClose })
    {
        node->setParent(nullptr);
        node->setParent(&m_windowsLayer);
    }
}

void Expose::apply() noexcept
{
    const SkScalar p { SkScalar(std::max(0.0, m_progress)) };
    placeWindowsClip(); // The output may have moved/resized
    const SkScalar bar { std::clamp(p, 0.f, 1.f) };

    for (auto &w : m_windows)
    {
        if (!w->laidOut || w->from.isEmpty())
            continue;

        // Where its workspace is (sliding), shown while it's in view
        const Workspaces *ws { w->output->workspaces.get() };
        const SkScalar dx { ws ? SkScalar(ws->slideX(w->workspace)) : 0.f };
        w->node.setVisible(!ws || std::abs(Float64(w->workspace) - ws->pos()) < 0.999);

        // Being dragged: where the pointer takes it (see dragWindowTo())
        if (w.get() == m_pressWindow && m_windowDragging)
            continue;

        // Its shadow scaled with it
        const SkRect r { Lerp(w->from, w->slot, p).makeOffset(dx, 0.f) };
        w->shown = r;
        const SkScalar s { r.width() / w->from.width() };
        w->node.layout().setPosition(YGEdgeLeft, r.x() - w->margins.left() * s);
        w->node.layout().setPosition(YGEdgeTop, r.y() - w->margins.top() * s);
        w->node.layout().setWidth(std::max(1.f, r.width() + (w->margins.left() + w->margins.right()) * s));
        w->node.layout().setHeight(std::max(1.f, r.height() + (w->margins.top() + w->margins.bottom()) * s));
    }

    placeWindowOutline();

    // Its output's top bars fade out as they slide up (their blur too, see TopbarHideProgress())
    const SkScalar topbarOpacity { 1.f - SkScalar(TopbarHideProgress(bar)) };

    for (Surface *surface : GetSurfaces())
    {
        if (surface->parent() || !surface->layerRole() || !TopbarAutoHide::IsTopbar(*surface->layerRole()) ||
            surface->layerRole()->exclusiveOutput() != m_output)
            continue;

        surface->view.view.setOpacity(topbarOpacity);

        if (auto *blur { static_cast<SurfaceBlurManager*>(surface->backgroundBlur()) })
            blur->setCompositorOpacity(topbarOpacity);
    }

    for (auto &screen : m_screens)
    {
        // The outline follows the current workspace
        if (const Workspaces *ws { screen->output->workspaces.get() })
            for (auto &thumb : screen->thumbs)
                thumb->frame.setVisible(thumb->workspace == ws->current() && thumb.get() != m_zoomThumb && thumb->box.visible());

        const SkIRect &r { screen->output->rect() };
        screen->bar.layout().setPosition(YGEdgeLeft, r.x());
        screen->bar.layout().setPosition(YGEdgeTop, r.y() - screen->barHeight * (1.f - bar));
        screen->bar.setVisible(bar > 0.f);
    }

    compositor()->repaintAllOutputs();
}

/* ------------------------------------------ State ------------------------------------------ */

void Expose::activate() noexcept
{
    if (m_active)
        return;

    // Where the cursor is (still there: outputs may come and go)
    m_output = static_cast<Output*>(cursor()->output());

    if (!m_output && !compositor()->outputs().empty())
        m_output = static_cast<Output*>(compositor()->outputs().front());

    if (!m_output)
        return;

    m_active = true;
    notify(true);
    build();

    // The popups of the windows laid out (not the fullscreen workspaces', shown as they are)
    std::vector<LPopupRole*> popups;

    for (LSurface *s : compositor()->surfaces())
        if (s->popup() && s->mapped() && HidesSurface(s))
            popups.push_back(s->popup());

    for (LPopupRole *popup : popups)
        popup->dismiss();

    apply();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();
}

void Expose::deactivate() noexcept
{
    if (!m_active)
        return;

    m_active = false;

    for (Surface *surface : GetSurfaces())
    {
        if (surface->parent() || !surface->layerRole() || !TopbarAutoHide::IsTopbar(*surface->layerRole()))
            continue;

        surface->view.view.setOpacity(1.f);

        if (auto *blur { static_cast<SurfaceBlurManager*>(surface->backgroundBlur()) })
            blur->setCompositorOpacity(1.f);
    }

    // Destroyed on the next opening (this may be an animation step, on a rendering thread)
    for (auto &w : m_windows)
        w->node.setVisible(false);

    // The hovered window's outline and close button too (they're not windows)
    m_hoverWindow = nullptr;
    m_windowOutline.setVisible(false);
    m_windowClose.setVisible(false);
    m_windowCloseShadow.setVisible(false);

    for (auto &screen : m_screens)
        screen->bar.setVisible(false);

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();

    compositor()->repaintAllOutputs();
}

void Expose::animateTo(Float64 target, Float64 velocity) noexcept
{
    m_open = target > 0.5;
    notify(m_open);

    if (m_open)
        activate();
    else
    {
        // However it's closed (swipe, keys, click): no outline meanwhile
        setHoverWindow(nullptr);
        placeWindowOutline();
    }

    m_spring.setStiffness(m_open ? SpringStiffness : CloseStiffness);
    m_spring.setFrom(m_progress);
    m_spring.setTo(target);
    m_spring.setVelocity(velocity);
    m_spring.reset();
}

void Expose::close(ToplevelRole *toplevel) noexcept
{
    endPress(false);
    endWindowPress(false);
    setHoverWindow(nullptr);

    // The chosen window: activated, on top, back where it was
    if (toplevel)
    {
        toplevel->surface()->raise();
        toplevel->configureState(toplevel->pendingConfiguration().windowState | CZWinActivated);

        if (GetCompositor()->deskActive())
        {
            GetCompositor()->deactivateDesk();
            GetCompositor()->updateActiveHeavenClient();
        }

        for (auto &w : m_windows)
            if (w->toplevel.get() == toplevel)
                w->node.setParent(&GetScene()->expose);

        for (auto &screen : m_screens)
            screen->bar.setParent(&GetScene()->expose);
    }

    animateTo(0.0, 0.0);
}

void Expose::Toggle() noexcept
{
    Expose *e { Get(true) };

    if (e->m_zoomThumb)
        return;

    if (e->m_open)
        e->close();
    else
        e->animateTo(1.0, 0.0);
}

void Expose::WindowsChanged() noexcept
{
    Expose *e { Get(false) };

    if (!e || !e->m_active || !e->m_open)
        return;

    e->build();
    e->apply();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();
}

void Expose::notify(bool shown) noexcept
{
    if (m_notified == shown)
        return;

    m_notified = shown;

    if (!m_bus)
        m_bus = CZBus::GetOrMakeUser();

    if (!m_bus)
        return;

    auto &bus { m_bus };
    sd_bus_emit_signal(bus->bus(), "/org/cuarzo/Crystals", "org.cuarzo.Crystals", "ExposeChanged", "bs", int(shown),
                       m_output ? m_output->name().c_str() : "");
    sd_bus_flush(bus->bus());
}

void Expose::WorkspacesMoved() noexcept
{
    if (Active())
        s_expose->apply();
}

void Expose::ProcessPending(Output &output) noexcept
{
    Expose *e { Get(false) };

    if (!e || !e->m_active)
        return;

    // Workspaces removed (not while zooming: its thumbnail is in use)
    if (e->m_rebuildPending && e->m_open && !e->m_zoomThumb)
    {
        e->m_rebuildPending = false;
        e->build();
        e->apply();
    }

    for (auto &w : e->m_windows)
        if (w->dirty && w->output == &output)
            e->snapshot(*w);
}

void Expose::SurfaceDamaged(const LSurface *surface) noexcept
{
    Expose *e { Get(false) };

    if (!e || !e->m_active || !surface)
        return;

    const LSurface *root { surface->parent() ? surface->topmostParent() : surface };

    for (auto &w : e->m_windows)
        if (w->toplevel && w->toplevel->surface() == root)
        {
            // Taken again on its output's next frame (see ProcessPending()), which also answers its
            // frame callbacks
            w->dirty = true;

            if (w->output)
                w->output->repaint();
            return;
        }
}

/* ------------------------------------------ Input ------------------------------------------ */

bool Expose::KeyEvent(const CZKeyboardKeyEvent &e) noexcept
{
    const auto *keyboard { seat()->keyboard() };

    if (Active() && s_expose->m_zoomThumb)
        return e.code == KEY_ESC || e.code == KEY_UP || e.code == KEY_DOWN;

    if (e.code == KEY_ESC && Active())
    {
        if (e.isPressed && s_expose->m_open)
            s_expose->close();

        return true;
    }

    if (e.code != KEY_UP && e.code != KEY_DOWN)
        return false;

    const bool ctrl { keyboard->isKeyCodePressed(KEY_LEFTCTRL) || keyboard->isKeyCodePressed(KEY_RIGHTCTRL) };
    const bool super { keyboard->isKeyCodePressed(KEY_LEFTMETA) || keyboard->isKeyCodePressed(KEY_RIGHTMETA) };

    if (!ctrl || !super)
        return false;

    if (e.isPressed && !e.isRepeat)
    {
        if (e.code == KEY_UP)
            Toggle();
        else if (s_expose && s_expose->m_open)
            s_expose->close();
    }

    return true;
}

void Expose::SwipeBegin() noexcept
{
    Expose *e { Get(true) };

    if (e->m_zoomThumb)
        return;

    e->m_swiping = true;
    e->m_swipeRaw = e->m_spring.isRunning() ? e->m_spring.to() : e->m_progress;
    e->m_swipeVelocity = 0.0;
    e->m_swipeLast = std::chrono::steady_clock::now();
}

void Expose::SwipeUpdate(const SkPoint &delta) noexcept
{
    Expose *e { Get(false) };

    if (!e || !e->m_swiping)
        return;

    // Fingers up: opens. Resisting past the ends (rubber band, see Workspaces).
    constexpr Float64 maxOver { 0.05 }, slope { 0.25 };
    const auto rubber = [](Float64 x){ return maxOver * x / (x + maxOver / slope); };
    e->m_swipeRaw = std::clamp(e->m_swipeRaw - delta.y() / SwipeDistance, -1.0, 2.0);
    const Float64 raw { e->m_swipeRaw };
    const Float64 target { raw < 0.0 ? -rubber(-raw) : raw > 1.0 ? 1.0 + rubber(raw - 1.0) : raw };

    const auto now { std::chrono::steady_clock::now() };
    const Float64 dt { std::chrono::duration<Float64>(now - e->m_swipeLast).count() };
    const Float64 prev { e->m_spring.isRunning() ? e->m_spring.to() : e->m_progress };
    e->m_swipeLast = now;

    if (dt > 0.0)
        e->m_swipeVelocity = 0.6 * ((target - prev) / std::max(dt, 0.004)) + 0.4 * e->m_swipeVelocity;

    // Shown once it starts opening (not for a swipe down while closed)
    if (target > 0.0)
        e->activate();

    // Followed through a stiff spring (smooth, see Workspaces)
    e->m_spring.setStiffness(SwipeFollowStiffness);

    if (e->m_spring.isRunning())
        e->m_spring.setTo(target);
    else
    {
        e->m_spring.setFrom(e->m_progress);
        e->m_spring.setTo(target);
        e->m_spring.setVelocity(0.0);
        e->m_spring.reset();
    }
}

void Expose::SwipeEnd(bool cancelled) noexcept
{
    Expose *e { Get(false) };

    if (!e || !e->m_swiping)
        return;

    e->m_swiping = false;

    const Float64 idle { std::chrono::duration<Float64>(std::chrono::steady_clock::now() - e->m_swipeLast).count() };
    const Float64 fingers { cancelled || idle > 0.1 ? 0.0 : e->m_swipeVelocity };
    const Float64 at { e->m_spring.isRunning() ? e->m_spring.to() : e->m_progress };

    // Flicked, or released past halfway (cancelled: back where it was)
    bool open { e->m_open };

    if (!cancelled)
        open = fingers > 0.8 || (fingers > -0.8 && at > 0.5);

    const Float64 velocity { e->m_spring.isRunning() ? std::clamp(e->m_spring.velocity(), -5.0, 5.0) : 0.0 };
    e->animateTo(open ? 1.0 : 0.0, velocity);
}

// Over the dock (it keeps working): a shell surface other than the top bar (covered by the bar).
static bool OverDock(const SkPoint &pos) noexcept
{
    LSurface *s { seat()->surfaceAt(SkIPoint::Make(Int32(std::floor(pos.x())), Int32(std::floor(pos.y())))) };

    if (!s || !Crystals::IsShell(s))
        return false;

    const LSurface *root { s->parent() ? s->topmostParent() : s };
    return !(root->layerRole() && TopbarAutoHide::IsTopbar(*root->layerRole()));
}

void Expose::setHovered(Screen &screen, size_t workspace) noexcept
{
    // Not the desktop (it can't be removed), nor while closing/zooming
    if (workspace == 0 || workspace >= screen.thumbs.size() || !m_open || m_zoomThumb)
        workspace = SIZE_MAX;

    if (screen.hovered == workspace)
        return;

    screen.hovered = workspace;
    screen.closeButton.setVisible(workspace != SIZE_MAX);
    screen.closeShadow.setVisible(workspace != SIZE_MAX);

    if (workspace == SIZE_MAX)
        return;

    // Centered on its top-left corner
    const SkRect &r { screen.thumbs[workspace]->rect };
    screen.closeRect = SkRect::MakeXYWH(r.x() - CloseSize / 2.f, r.y() - CloseSize / 2.f, CloseSize, CloseSize);
    screen.closeButton.layout().setPosition(YGEdgeLeft, screen.closeRect.x());
    screen.closeButton.layout().setPosition(YGEdgeTop, screen.closeRect.y());
    screen.closeShadow.layout().setPosition(YGEdgeLeft, screen.closeRect.x() - CloseShadowPad);
    screen.closeShadow.layout().setPosition(YGEdgeTop, screen.closeRect.y() - CloseShadowPad + 2); // Cast slightly down
}

bool Expose::PointerMove() noexcept
{
    if (!Active())
        return false;

    const SkPoint pos { cursor()->pos() };
    const bool onIt { cursor()->output() == s_expose->m_output };

    // Dragging a thumbnail or a window (no hover meanwhile)
    if (s_expose->m_pressThumb)
    {
        s_expose->dragTo(pos);
        return true;
    }

    if (s_expose->m_pressWindow)
    {
        s_expose->dragWindowTo(pos);
        return true;
    }

    // Left its output: closed, the pointer goes on as usual (to the other output's surfaces)
    if (!onIt && s_expose->m_open && !s_expose->m_zoomThumb && !s_expose->m_swiping)
    {
        s_expose->close();
        return false;
    }

    // The hovered window (not under the bar)
    {
        Screen *barScreen { nullptr };
        const bool overBar { s_expose->thumbAt(pos, &barScreen) != nullptr };
        Window *hover { nullptr };

        if (onIt && !overBar && s_expose->m_open && !s_expose->m_zoomThumb)
        {
            // Over its close button (partly outside it): still that one
            if (s_expose->m_hoverWindow && s_expose->m_windowClose.visible() && s_expose->m_windowCloseRect.contains(pos.x(), pos.y()))
                hover = s_expose->m_hoverWindow;
            else
                hover = s_expose->windowAt(pos);
        }

        s_expose->setHoverWindow(hover);
    }
    Expose &x { *s_expose };
    const SkScalar p { SkScalar(std::clamp(x.m_progress, 0.0, 1.0)) };

    // The hovered thumbnail (or its close button, partly outside it)
    for (auto &screen : x.m_screens)
    {
        const SkIRect &r { screen->output->rect() };
        const SkPoint bar { SkScalar(r.x()), r.y() - screen->barHeight * (1.f - p) };
        size_t hovered { SIZE_MAX };

        if (screen->hovered != SIZE_MAX && screen->closeRect.makeOffset(bar).contains(pos.x(), pos.y()))
            hovered = screen->hovered;
        else
            for (auto &t : screen->thumbs)
                if (t->rect.makeOffset(bar).contains(pos.x(), pos.y()))
                    hovered = t->workspace;

        x.setHovered(*screen, onIt ? hovered : SIZE_MAX);

        // The plus button (or the new desktop peeking, while shown)
        const bool plus { onIt && (screen->plusRect.makeOffset(bar).contains(pos.x(), pos.y()) ||
                                   (screen->plusHovered && screen->peekRect.makeOffset(bar).contains(pos.x(), pos.y()))) };
        x.setPlusHovered(*screen, plus);
    }

    // Other outputs: as usual
    return onIt && !OverDock(pos);
}

void Expose::animateRemoval(Screen &screen, Thumb &thumb) noexcept
{
    m_removed.remove_if([](const auto &r){ return r->done; });

    auto &r { m_removed.emplace_back(std::make_unique<Removed>()) };
    const SkIRect &out { screen.output->rect() };
    const SkScalar p { SkScalar(std::clamp(m_progress, 0.0, 1.0)) };
    r->rect = thumb.rect.makeOffset(out.x(), out.y() - screen.barHeight * (1.f - p));

    // Above everything of Exposé, clipped like the thumbnail
    const SkRegion empty;
    r->box.setInputRegion(&empty);
    r->box.enableChildrenClipping(true);
    r->box.layout().setPositionType(YGPositionTypeAbsolute);
    r->box.setParent(&GetScene()->expose);

    r->fill.setInputRegion(&empty);
    r->fill.layout().setPositionType(YGPositionTypeAbsolute);
    r->fill.layout().setWidthPercent(100.f);
    r->fill.layout().setHeightPercent(100.f);
    r->fill.setParent(&r->box);

    // Its windows as laid out in it (fractions of it: they shrink with it)
    const SkScalar w { std::max(1.f, thumb.rect.width()) }, h { std::max(1.f, thumb.rect.height()) };

    for (auto &win : m_windows)
    {
        if (win->output != screen.output || win->workspace != thumb.workspace || !win->thumb.image())
            continue;

        auto &l { win->thumb.layout() };
        AKImage &img { r->images.emplace_back() };
        img.setInputRegion(&empty);
        img.setImage(win->thumb.image());
        img.layout().setPositionType(YGPositionTypeAbsolute);
        img.layout().positionPercent(YGEdgeLeft, 100.f * l.calculatedLeft() / w);
        img.layout().positionPercent(YGEdgeTop, 100.f * l.calculatedTop() / h);
        img.layout().setWidthPercent(100.f * l.calculatedWidth() / w);
        img.layout().setHeightPercent(100.f * l.calculatedHeight() / h);
        img.setParent(&r->box);
    }

    Removed *raw { r.get() };
    const auto step = [raw](Float64 t)
    {
        const SkScalar e { SkScalar(CZEase::OutCubic(t)) };
        const SkScalar s { 1.f - 0.35f * e };
        const SkRect &f { raw->rect };
        const SkRect rect { SkRect::MakeXYWH(f.centerX() - f.width() * s * 0.5f, f.centerY() - f.height() * s * 0.5f,
                                             f.width() * s, f.height() * s) };
        raw->box.layout().setPosition(YGEdgeLeft, rect.x());
        raw->box.layout().setPosition(YGEdgeTop, rect.y());
        raw->box.layout().setWidth(std::max(1.f, rect.width()));
        raw->box.layout().setHeight(std::max(1.f, rect.height()));
        raw->fill.setOpacity(1.f - e);

        for (AKImage &img : raw->images)
            img.setOpacity(1.f - e);

        compositor()->repaintAllOutputs();
    };

    r->anim.setDuration(RemoveMs);
    r->anim.onUpdate.subscribe(this, [step](CZAnimation *a){ step(a->value()); });
    r->anim.onFinish.subscribe(this, [raw, step](CZAnimation*){
        step(1.0);
        raw->box.setVisible(false); // Destroyed later (this may be a rendering thread)
        raw->done = true;
    });
    step(0.0);
    r->anim.start();
}

std::vector<Expose::Thumb*> Expose::shownThumbs(Screen &screen) noexcept
{
    std::vector<Thumb*> list;

    for (auto &t : screen.thumbs)
        if (t->box.visible())
            list.push_back(t.get());

    return list;
}

void Expose::placeThumb(Thumb &thumb, SkScalar x) noexcept
{
    thumb.rect.offsetTo(x, thumb.rect.y());
    thumb.box.layout().setPosition(YGEdgeLeft, x);
    thumb.frame.layout().setPosition(YGEdgeLeft, x - thumb.frame.thickness() - ThumbOutlineGap);
}

void Expose::dragTo(const SkPoint &pos) noexcept
{
    Screen &screen { *m_pressScreen };
    Thumb &dragged { *m_pressThumb };

    if (!m_dragging)
    {
        if (SkPoint::Distance(pos, m_pressPos) < DragThreshold)
            return;

        // Picked up: above the others, no close button meanwhile
        m_dragging = true;
        setHovered(screen, SIZE_MAX);
        dragged.frame.setParent(&screen.bar);
        dragged.box.setParent(&screen.bar);
    }

    const auto shown { shownThumbs(screen) };

    if (shown.size() < 3 || screen.slots.size() != shown.size()) // Only the desktop and itself: nowhere to go
        return;

    // Following the pointer, within the fullscreen workspaces' slots (the desktop's is the first)
    const SkScalar barX { SkScalar(screen.output->rect().x()) };
    const SkScalar x { std::clamp(pos.x() - barX - m_grabX, screen.slots[1], screen.slots.back()) };
    placeThumb(dragged, x);

    // The slot it's closest to; the others fill the rest in order
    size_t drop { 1 };

    for (size_t i = 1; i < screen.slots.size(); i++)
        if (std::abs(screen.slots[i] - x) < std::abs(screen.slots[drop] - x))
            drop = i;

    m_dropSlot = drop;
    size_t slot { 1 };

    for (size_t i = 1; i < shown.size(); i++)
    {
        if (shown[i] == &dragged)
            continue;

        if (slot == drop)
            slot++;

        placeThumb(*shown[i], screen.slots[slot++]);
    }

    compositor()->repaintAllOutputs();
}

void Expose::endPress(bool released) noexcept
{
    Screen *screen { m_pressScreen };
    Thumb *thumb { m_pressThumb };
    const bool dragged { m_dragging };
    m_pressScreen = nullptr;
    m_pressThumb = nullptr;
    m_dragging = false;

    if (!screen || !thumb || !released || !m_open)
        return;

    // A click: grows into it (the current one: back to it, no zoom)
    if (!dragged)
    {
        if (Workspaces *ws { Workspaces::Of(screen->output) }; ws && thumb->workspace == ws->current())
            close();
        else
            zoomInto(*screen, *thumb);
        return;
    }

    // Dropped: moved there (the slot's workspace, skipping those being removed)
    const auto shown { shownThumbs(*screen) };
    Workspaces *ws { Workspaces::Of(screen->output) };

    if (ws && m_dropSlot < shown.size())
    {
        // Slots count shown thumbnails only (not those being removed): the workspace in that slot
        const size_t to { shown[m_dropSlot]->workspace };

        if (thumb->workspace != to)
            ws->move(thumb->workspace, to);
    }

    // Rebuilt in the new order
    build();
    apply();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();
}

// Places the peek (or the new desktop growing from it) at @p rect (in the bar).
static void PlacePeek(AKContainer &peek, AKImage &topbar, const SkRect &rect, const SkRect &outputRect, const SkRect &topbarFrom) noexcept
{
    peek.layout().setPosition(YGEdgeLeft, rect.x());
    peek.layout().setPosition(YGEdgeTop, rect.y());
    peek.layout().setWidth(std::max(1.f, rect.width()));
    peek.layout().setHeight(std::max(1.f, rect.height()));

    // Its top bar, scaled like the thumbnails'
    const SkScalar fit { rect.width() / std::max(1.f, outputRect.width()) };
    topbar.layout().setPosition(YGEdgeLeft, (topbarFrom.x() - outputRect.x()) * fit);
    topbar.layout().setPosition(YGEdgeTop, (topbarFrom.y() - outputRect.y()) * fit);
    topbar.layout().setWidth(std::max(1.f, topbarFrom.width() * fit));
    topbar.layout().setHeight(std::max(1.f, topbarFrom.height() * fit));
}

void Expose::setPlusHovered(Screen &screen, bool hovered) noexcept
{
    if (m_addScreen == &screen || !m_open || m_zoomThumb)
        hovered = m_addScreen == &screen; // Kept while it grows into the bar

    if (screen.plusHovered == hovered)
        return;

    screen.plusHovered = hovered;
    screen.peek.setVisible(hovered);

    if (hovered && m_addScreen != &screen)
    {
        screen.peekTopbar.setImage(screen.topbarImage);
        screen.peekTopbar.setVisible(screen.topbarImage != nullptr);
        PlacePeek(screen.peek, screen.peekTopbar, screen.peekRect, SkRect::Make(screen.output->rect()), screen.topbarFrom);
    }

    compositor()->repaintAllOutputs();
}

void Expose::addDesktop(Screen &screen) noexcept
{
    if (m_addScreen)
        return;

    Workspaces *ws { Workspaces::Of(screen.output) };

    if (!ws)
        return;

    // Created right away (the bar is rebuilt once it got there)
    ws->addDesktop();
    m_addScreen = &screen;
    setHovered(screen, SIZE_MAX);

    // The others make room: the same slots for one more, centered
    const auto shown { shownThumbs(screen) };
    const SkScalar barW { SkScalar(screen.output->rect().width()) };
    const SkScalar w { screen.thumbWidth };
    const size_t n { shown.size() + 1 };
    const SkScalar x0 { std::round((barW - (n * w + (n - 1) * ThumbGap)) * 0.5f) };

    m_addMoves.clear();

    for (size_t i = 0; i < shown.size(); i++)
        m_addMoves.push_back({ shown[i], { shown[i]->rect.x(), x0 + i * (w + ThumbGap) } });

    m_addFrom = screen.peekRect;
    m_addTo = SkRect::MakeXYWH(x0 + shown.size() * (w + ThumbGap), BarPadding, w, screen.thumbHeight);

    // Above the plus button while it grows
    screen.peek.setParent(&screen.bar);
    screen.plusButton.setVisible(false);
    screen.plusShadow.setVisible(false);

    m_addAnim.setDuration(AddMs);
    m_addAnim.reset();
}

void Expose::addStep(Float64 t) noexcept
{
    if (!m_addScreen)
        return;

    Screen &screen { *m_addScreen };
    const SkScalar e { SkScalar(CZEase::InOutCubic(std::clamp(t, 0.0, 1.0))) };

    for (auto &[thumb, x] : m_addMoves)
        placeThumb(*thumb, x.first + (x.second - x.first) * e);

    const SkRect rect { Lerp(m_addFrom, m_addTo, e) };
    PlacePeek(screen.peek, screen.peekTopbar, rect, SkRect::Make(screen.output->rect()), screen.topbarFrom);
    compositor()->repaintAllOutputs();
}

void Expose::setHoverWindow(Window *window) noexcept
{
    if (m_hoverWindow == window)
        return;

    m_hoverWindow = window;
    placeWindowOutline();
}

SkRect Expose::windowGeometryShown(const Window &w) const noexcept
{
    // Its geometry within where it's drawn (scaled with it)
    const SkScalar s { w.shown.width() / std::max(1.f, w.from.width()) };
    return SkRect::MakeXYWH(w.shown.x() + w.geometry.x() * s, w.shown.y() + w.geometry.y() * s,
                            w.geometry.width() * s, w.geometry.height() * s);
}

void Expose::placeWindowOutline() noexcept
{
    Window *w { m_windowDragging || !m_open ? nullptr : m_hoverWindow };

    if (!w || !w->node.visible() || w->shown.isEmpty())
    {
        m_windowOutline.setVisible(false);
        m_windowClose.setVisible(false);
        m_windowCloseShadow.setVisible(false);
        return;
    }

    // Around the window itself (its corners scaled with it)
    const SkRect g { windowGeometryShown(*w) };
    const SkScalar s { w->shown.width() / std::max(1.f, w->from.width()) };
    const Int32 radius { std::max(2, Int32(std::lround(SSDTheme::BorderRadius * s))) };
    const Int32 out { m_windowOutline.thickness() + 2 };
    m_windowOutline.setRadius(radius + 2);
    m_windowOutline.layout().setPosition(YGEdgeLeft, g.x() - out);
    m_windowOutline.layout().setPosition(YGEdgeTop, g.y() - out);
    m_windowOutline.layout().setWidth(g.width() + 2 * out);
    m_windowOutline.layout().setHeight(g.height() + 2 * out);
    m_windowOutline.setVisible(true);

    // Its close button, centered on its top-left corner
    m_windowCloseRect = SkRect::MakeXYWH(g.x() - CloseSize / 2.f, g.y() - CloseSize / 2.f, CloseSize, CloseSize);
    m_windowClose.layout().setPosition(YGEdgeLeft, m_windowCloseRect.x());
    m_windowClose.layout().setPosition(YGEdgeTop, m_windowCloseRect.y());
    m_windowCloseShadow.layout().setPosition(YGEdgeLeft, m_windowCloseRect.x() - CloseShadowPad);
    m_windowCloseShadow.layout().setPosition(YGEdgeTop, m_windowCloseRect.y() - CloseShadowPad + 2);
    m_windowClose.setVisible(true);
    m_windowCloseShadow.setVisible(true);
}

void Expose::dragWindowTo(const SkPoint &pos) noexcept
{
    Window &w { *m_pressWindow };

    if (!m_windowDragging)
    {
        if (SkPoint::Distance(pos, m_pressPos) < DragThreshold)
            return;

        // Picked up: above everything of Exposé (the bar too), see-through
        m_windowDragging = true;
        w.node.setParent(&GetScene()->expose);
        w.node.setOpacity(DraggedOpacity);
        placeWindowOutline();
    }

    // Following the pointer (where it was grabbed)
    const SkRect &r { w.shown };
    const SkRect rect { SkRect::MakeXYWH(pos.x() - m_windowGrab.x() * r.width(), pos.y() - m_windowGrab.y() * r.height(), r.width(), r.height()) };
    const SkScalar s { rect.width() / std::max(1.f, w.from.width()) };
    w.node.layout().setPosition(YGEdgeLeft, rect.x() - w.margins.left() * s);
    w.node.layout().setPosition(YGEdgeTop, rect.y() - w.margins.top() * s);

    // Over another desktop's thumbnail: outlined
    Screen *screen { nullptr };
    Thumb *thumb { thumbAt(pos, &screen) };
    const Workspaces *ws { w.output->workspaces.get() };

    if (thumb && (!ws || !ws->isDesktop(thumb->workspace) || thumb->workspace == w.workspace))
        thumb = nullptr;

    if (m_dropThumb != thumb)
    {
        if (m_dropThumb)
            m_dropThumb->frame.setVisible(ws && m_dropThumb->workspace == ws->current());

        m_dropThumb = thumb;

        if (thumb)
            thumb->frame.setVisible(true);
    }

    compositor()->repaintAllOutputs();
}

void Expose::endWindowPress(bool released) noexcept
{
    Window *w { m_pressWindow };
    const bool dragged { m_windowDragging };
    Thumb *drop { m_dropThumb };
    m_pressWindow = nullptr;
    m_windowDragging = false;
    m_dropThumb = nullptr;

    if (!w)
        return;

    w->node.setOpacity(1.f);

    if (!released || !m_open)
    {
        if (dragged)
        {
            restackWindows(); // Back among the others (clipped)
            apply();          // Back where it was
        }
        return;
    }

    // A click: activated
    if (!dragged)
    {
        close(w->toplevel.get());
        return;
    }

    // Dropped on another desktop's thumbnail: moved there; otherwise back where it was (rebuilt)
    Workspaces *ws { w->output->workspaces.get() };

    if (drop && ws && w->toplevel)
        w->toplevel->desktopId = ws->desktopId(drop->workspace);

    build();
    apply();

    for (Surface *surface : GetSurfaces())
        surface->view.syncVisibility();
}

void Expose::WorkspacesChanged() noexcept
{
    if (!Active())
        return;

    s_expose->m_rebuildPending = true;
    compositor()->repaintAllOutputs();
}

Expose::Window *Expose::windowAt(const SkPoint &pos) noexcept
{
    const SkScalar p { SkScalar(std::clamp(m_progress, 0.0, 1.0)) };

    for (auto it = m_windows.rbegin(); it != m_windows.rend(); it++)
    {
        const Window &w { **it };
        const Workspaces *ws { w.output->workspaces.get() };

        if (!w.laidOut || !ws || w.workspace != ws->current())
            continue;

        // The window itself (not client-drawn shadows)
        const SkRect r { Lerp(w.from, w.slot, p).makeOffset(SkScalar(ws->slideX(w.workspace)), 0.f) };
        const SkScalar sc { r.width() / std::max(1.f, w.from.width()) };
        const SkRect g { SkRect::MakeXYWH(r.x() + w.geometry.x() * sc, r.y() + w.geometry.y() * sc,
                                          w.geometry.width() * sc, w.geometry.height() * sc) };

        if (g.contains(pos.x(), pos.y()))
            return it->get();
    }

    return nullptr;
}

Expose::Thumb *Expose::thumbAt(const SkPoint &pos, Screen **screen) noexcept
{
    const SkScalar p { SkScalar(std::clamp(m_progress, 0.0, 1.0)) };

    for (auto &s : m_screens)
    {
        const SkIRect &r { s->output->rect() };
        const SkPoint barPos { SkScalar(r.x()), r.y() - s->barHeight * (1.f - p) };

        for (auto &t : s->thumbs)
            if (t->rect.makeOffset(barPos).contains(pos.x(), pos.y()))
            {
                *screen = s.get();
                return t.get();
            }
    }

    return nullptr;
}

bool Expose::PointerButton(const CZPointerButtonEvent &e) noexcept
{
    if (!Active())
        return false;

    const SkPoint pos { cursor()->pos() };
    Expose &x { *s_expose };

    // A pressed thumbnail / window released (wherever): dropped (dragged) or clicked
    if (e.button == BTN_LEFT && !e.pressed && x.m_pressThumb)
    {
        x.endPress(true);
        return true;
    }

    if (e.button == BTN_LEFT && !e.pressed && x.m_pressWindow)
    {
        x.endWindowPress(true);
        return true;
    }

    // Other outputs (and the dock): as usual
    if (cursor()->output() != s_expose->m_output || OverDock(pos))
        return false;

    if (e.button == BTN_LEFT && !e.pressed)
        return true;

    if (e.button != BTN_LEFT || !e.pressed || !s_expose->m_open)
        return true;

    Screen *screen { nullptr };

    // The plus button (or the new desktop peeking): adds a desktop
    for (auto &s : x.m_screens)
    {
        const SkIRect &r { s->output->rect() };
        const SkPoint bar { SkScalar(r.x()), r.y() - s->barHeight * (1.f - SkScalar(std::clamp(x.m_progress, 0.0, 1.0))) };

        if (s->plusHovered && !x.m_addScreen && (s->plusRect.makeOffset(bar).contains(pos.x(), pos.y()) ||
                                                 s->peekRect.makeOffset(bar).contains(pos.x(), pos.y())))
        {
            x.addDesktop(*s);
            return true;
        }
    }

    // A close button: removes its workspace (its window back to the desktop, shown here right away)
    for (auto &s : x.m_screens)
    {
        if (s->hovered == SIZE_MAX)
            continue;

        const SkIRect &r { s->output->rect() };
        const SkPoint bar { SkScalar(r.x()), r.y() - s->barHeight * (1.f - SkScalar(std::clamp(x.m_progress, 0.0, 1.0))) };

        if (!s->closeRect.makeOffset(bar).contains(pos.x(), pos.y()))
            continue;

        const size_t index { s->hovered };
        x.setHovered(*s, SIZE_MAX);

        if (index < s->thumbs.size())
            x.animateRemoval(*s, *s->thumbs[index]);

        if (Workspaces *ws { Workspaces::Of(s->output) })
            ws->destroy(index, false);

        WindowsChanged();
        return true;
    }

    // A workspace: the desktop grows into it right away, fullscreen ones on release (unless dragged)
    if (Thumb *thumb { x.thumbAt(pos, &screen) })
    {
        // The current one: back to it (no zoom)
        if (Workspaces *ws { Workspaces::Of(screen->output) }; ws && thumb->workspace == ws->current())
        {
            x.close();
            return true;
        }

        if (thumb->workspace == 0)
        {
            x.zoomInto(*screen, *thumb);
            return true;
        }

        const SkIRect &r { screen->output->rect() };
        const SkScalar barX { SkScalar(r.x()) };
        x.m_pressScreen = screen;
        x.m_pressThumb = thumb;
        x.m_pressPos = pos;
        x.m_grabX = pos.x() - barX - thumb->rect.x();
        x.m_dragging = false;
        return true;
    }

    // A window's close button: asked to close (Exposé rebuilds once it's gone)
    if (x.m_hoverWindow && x.m_windowClose.visible() && x.m_windowCloseRect.contains(pos.x(), pos.y()))
    {
        if (ToplevelRole *toplevel { x.m_hoverWindow->toplevel.get() })
            toplevel->close();

        return true;
    }

    // A window: activated on release (unless dragged)
    if (Window *w { x.windowAt(pos) })
    {
        x.m_pressWindow = w;
        x.m_pressPos = pos;
        x.m_windowDragging = false;
        x.m_windowGrab = SkPoint::Make((pos.x() - w->shown.x()) / std::max(1.f, w->shown.width()),
                                       (pos.y() - w->shown.y()) / std::max(1.f, w->shown.height()));
        return true;
    }

    x.close();
    return true;
}

Expose::Screen *Expose::screenOf(const Output *output) noexcept
{
    for (auto &s : m_screens)
        if (s->output == output)
            return s.get();

    return nullptr;
}
