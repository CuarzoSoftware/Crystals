#include <Effects/Expose.h>
#include <Effects/Snapshot.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>
#include <Core/Crystals.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Roles/LayerRole.h>
#include <Scene/Scene.h>
#include <Seat/Output.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Core/CZTimer.h>
#include <Core/CZBus.h>
#include <Effects/SurfaceBlurManager.h>
#include <systemd/sd-bus.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Ream/RPainter.h>
#include <CZ/skia/core/SkCanvas.h>
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
    m_spring(0.0, 0.0, 0.0, SpringStiffness, CZSpringAnimation::DampingRatioNoBouncy,
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
    m_zoom(0.0, 1.0, 0.0, SpringStiffness, CZSpringAnimation::DampingRatioNoBouncy,
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

bool Expose::HidesSurface(const LSurface *surface) noexcept
{
    if (!Active() || !surface)
        return false;

    const LSurface *root { surface->parent() ? surface->topmostParent() : surface };
    const LToplevelRole *toplevel { root->toplevel() };

    if (!toplevel)
        return false;

    // Zooming into a workspace: it's shown (switched to) but the thumbnail covers it until the end
    if (s_expose->m_zoomScreen)
        if (Workspaces *ws { Workspaces::Find(root) }; ws && &ws->output() == s_expose->m_zoomScreen->output)
            return true;

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

void Expose::build() noexcept
{
    m_zoom.pause();
    m_zoomThumb = nullptr;
    m_zoomScreen = nullptr;
    m_windows.clear();
    m_screens.clear();

    AKContainer &layer { GetScene()->expose };

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

            if (!ws)
                continue;

            auto &w { m_windows.emplace_back(std::make_unique<Window>()) };
            w->toplevel.reset(toplevel);
            w->output = &ws->output();
            w->workspace = index;
            w->laidOut = true; // In its workspace's layout (see layoutScreen())
            w->from = WindowRect(*toplevel);

            const SkRegion empty;

            for (AKImage *node : { &w->node, &w->thumb })
            {
                node->setInputRegion(&empty);
                node->enableAutoDamage(true);
                node->layout().setPositionType(YGPositionTypeAbsolute);
            }

            w->node.setParent(&layer);

            snapshot(*w);
        }
    }

    // A bar on each output (above the windows)
    for (LOutput *o : compositor()->outputs())
    {
        auto &screen { m_screens.emplace_back(std::make_unique<Screen>()) };
        screen->output = static_cast<Output*>(o);
        layoutScreen(*screen);
    }
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

    screen.barFill.setInputRegion(&empty);
    screen.barFill.setColor(CZAdaptiveColor(dark ? 0x59000000 : 0x40FFFFFF));
    screen.barFill.layout().setPositionType(YGPositionTypeAbsolute);
    screen.barFill.layout().setWidthPercent(100.f);
    screen.barFill.layout().setHeightPercent(100.f);
    screen.barFill.setParent(&screen.bar);
    screen.blur.setFullSize();
    screen.blur.setColorScheme(scheme);
    screen.barFill.addBackgroundEffect(&screen.blur);

    const SkScalar total { count * thumbW + (count - 1) * ThumbGap };
    const SkScalar x0 { std::round((r.width() - total) * 0.5f) };
    const SkScalar fit { thumbW / std::max(1.f, r.width()) }; // Output -> thumbnail

    // The desktop picture
    std::shared_ptr<RImage> desktop;

    if (LSurface *desk { DeskOf(&output) })
        desktop = Snapshot::View(*static_cast<Surface*>(desk), desk->size(), SkIPoint::Make(0, 0), 1);

    for (size_t i = 0; i < count; i++)
    {
        auto &thumb { screen.thumbs.emplace_back(std::make_unique<Thumb>()) };
        thumb->workspace = i;
        thumb->rect = SkRect::MakeXYWH(x0 + i * (thumbW + ThumbGap), BarPadding, thumbW, thumbH);

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

        thumb->desktop.setImage(i == 0 ? desktop : nullptr);
        thumb->desktop.setVisible(i == 0 && desktop);
        thumb->topbar.setInputRegion(&empty);
        thumb->topbar.layout().setPositionType(YGPositionTypeAbsolute);
        thumb->topbar.setVisible(false);
    }

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

            Thumb &desk { *screen.thumbs.front() };
            // Where it is on the desktop (not slid away by a fullscreen workspace)
            const SkIPoint pos { static_cast<LayerRole*>(s->layerRole())->baseRolePos() };
            desk.topbarFrom = SkRect::Make(SkIRect::MakePtSize(pos, s->size()));
            desk.topbar.setImage(image);
            desk.topbar.setVisible(true);
            desk.topbar.setParent(&desk.box);
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

void Expose::apply() noexcept
{
    const SkScalar p { SkScalar(std::max(0.0, m_progress)) };
    const SkScalar bar { std::clamp(p, 0.f, 1.f) };

    for (auto &w : m_windows)
    {
        if (!w->laidOut || w->from.isEmpty())
            continue;

        // Where its workspace is (sliding), shown while it's in view
        const Workspaces *ws { w->output->workspaces.get() };
        const SkScalar dx { ws ? SkScalar(ws->slideX(w->workspace)) : 0.f };
        w->node.setVisible(!ws || std::abs(Float64(w->workspace) - ws->pos()) < 0.999);

        // Its shadow scaled with it
        const SkRect r { Lerp(w->from, w->slot, p).makeOffset(dx, 0.f) };
        const SkScalar s { r.width() / w->from.width() };
        w->node.layout().setPosition(YGEdgeLeft, r.x() - w->margins.left() * s);
        w->node.layout().setPosition(YGEdgeTop, r.y() - w->margins.top() * s);
        w->node.layout().setWidth(std::max(1.f, r.width() + (w->margins.left() + w->margins.right()) * s));
        w->node.layout().setHeight(std::max(1.f, r.height() + (w->margins.top() + w->margins.bottom()) * s));
    }

    // The top bars fade out as the workspace bar comes in (their blur too)
    for (Surface *surface : GetSurfaces())
    {
        if (surface->parent() || !surface->layerRole() || !TopbarAutoHide::IsTopbar(*surface->layerRole()))
            continue;

        surface->view.view.setOpacity(1.f - bar);

        if (auto *blur { static_cast<SurfaceBlurManager*>(surface->backgroundBlur()) })
            blur->fx.setOpacity(1.f - bar);
    }

    for (auto &screen : m_screens)
    {
        // The outline follows the current workspace
        if (const Workspaces *ws { screen->output->workspaces.get() })
            for (auto &thumb : screen->thumbs)
                thumb->frame.setVisible(thumb->workspace == ws->current() && thumb.get() != m_zoomThumb);

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

    m_active = true;
    notify(true);
    seat()->dismissPopups();
    build();
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
            blur->fx.setOpacity(1.f);
    }

    // Destroyed on the next opening (this may be an animation step, on a rendering thread)
    for (auto &w : m_windows)
        w->node.setVisible(false);

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

    m_spring.setStiffness(m_open ? SpringStiffness : CloseStiffness);
    m_spring.setFrom(m_progress);
    m_spring.setTo(target);
    m_spring.setVelocity(velocity);
    m_spring.reset();
}

void Expose::close(ToplevelRole *toplevel) noexcept
{
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
    sd_bus_emit_signal(bus->bus(), "/org/cuarzo/Crystals", "org.cuarzo.Crystals", "ExposeChanged", "b", int(shown));
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

bool Expose::PointerMove() noexcept
{
    return Active() && !OverDock(cursor()->pos());
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

        if (Lerp(w.from, w.slot, p).makeOffset(SkScalar(ws->slideX(w.workspace)), 0.f).contains(pos.x(), pos.y()))
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

    if (OverDock(pos))
        return false;

    if (e.button != BTN_LEFT || !e.pressed || !s_expose->m_open)
        return true;

    Expose &x { *s_expose };
    Screen *screen { nullptr };

    // A workspace: grows into it
    if (Thumb *thumb { x.thumbAt(pos, &screen) })
    {
        x.zoomInto(*screen, *thumb);
        return true;
    }

    // A window: activated
    if (Window *w { x.windowAt(pos) })
    {
        x.close(w->toplevel.get());
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
