#include <Effects/TopbarAutoHide.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Seat/Output.h>
#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LPointer.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LLayerRole.h>
#include <Louvre/Roles/LPopupRole.h>
#include <Core/CZEase.h>
#include <Core/Workspaces.h>
#include <Roles/LayerRole.h>
#include <Effects/SurfaceBlurManager.h>
#include <Scene/Scene.h>
#include <algorithm>
#include <cmath>

// Fully hidden bars are moved this far up: no input can reach them (e.g. from an output above).
static constexpr Int32 ParkedOffset { 1 << 20 };

static TopbarAutoHide *AutoHideOf(const LOutput *output) noexcept
{
    return output ? static_cast<const Output*>(output)->autoHide.get() : nullptr;
}

TopbarAutoHide::TopbarAutoHide(Output &output) noexcept :
    m_output(output),
    m_revealTimer([this](CZTimer*){ reveal(); }),
    m_animation(SlideMs,
        [this](CZAnimation *a){
            m_progress = m_from + (m_to - m_from) * CZEase::OutCubic(a->value());
            applyProgress();
        },
        [this](CZAnimation*){
            m_progress = m_to;
            applyProgress();
        })
{}

TopbarAutoHide::~TopbarAutoHide()
{
    m_revealTimer.stop();
    m_animation.pause();
}

void TopbarAutoHide::UpdateAll() noexcept
{
    for (LOutput *output : compositor()->outputs())
    {
        auto &autoHide { static_cast<Output*>(output)->autoHide };

        if (!autoHide)
            autoHide = std::make_unique<TopbarAutoHide>(*static_cast<Output*>(output));

        autoHide->update();
    }
}

bool TopbarAutoHide::IsTopbar(const LLayerRole &layerRole) noexcept
{
    const LSurfaceLayer layer { layerRole.layer() };

    return (layer == LLayerTop || layer == LLayerOverlay) &&
           layerRole.anchor().has(CZEdgeTop) && !layerRole.anchor().has(CZEdgeBottom) &&
           layerRole.exclusiveZoneSize() > 0;
}

Int32 TopbarAutoHide::TopbarOffset(const LLayerRole &layerRole) noexcept
{
    if (!IsTopbar(layerRole))
        return 0;

    const TopbarAutoHide *autoHide { AutoHideOf(layerRole.exclusiveOutput()) };

    if (!autoHide || !autoHide->m_fullscreen.get())
        return 0;

    if (autoHide->m_progress <= 0.0)
        return ParkedOffset;

    return autoHide->slide();
}

bool TopbarAutoHide::HidesSurface(const LSurface *surface) noexcept
{
    if (!surface || !surface->layerRole() || !IsTopbar(*surface->layerRole()))
        return false;

    const TopbarAutoHide *autoHide { AutoHideOf(surface->layerRole()->exclusiveOutput()) };
    return autoHide && autoHide->m_fullscreen.get() && autoHide->m_progress <= 0.0;
}

Int32 TopbarAutoHide::FullscreenTitlebarY(const ToplevelRole &toplevel) noexcept
{
    for (LOutput *output : compositor()->outputs())
    {
        const TopbarAutoHide *autoHide { AutoHideOf(output) };

        if (!autoHide)
            continue;

        if (autoHide->fullscreen() == &toplevel)
            return autoHide->topbarsHeight() - autoHide->slide();

        // Its workspace sliding away: as it was
        if (autoHide->leaving() == &toplevel)
            return autoHide->topbarsHeight() - autoHide->slideFor(autoHide->m_leavingProgress, &toplevel);
    }

    // Not the one hiding the bars (e.g. covered by another window): hidden
    return -SSDTheme::TitlebarHeight;
}

bool TopbarAutoHide::FullscreenTitlebarShown(const ToplevelRole &toplevel) noexcept
{
    return FullscreenTitlebarY(toplevel) > -SSDTheme::TitlebarHeight;
}

std::vector<LSurface*> TopbarAutoHide::topbars() const noexcept
{
    std::vector<LSurface*> bars;

    for (LSurfaceLayer layer : { LLayerTop, LLayerOverlay })
        for (LSurface *s : compositor()->layers()[layer])
            if (s->mapped() && s->layerRole() && s->layerRole()->exclusiveOutput() == &m_output && IsTopbar(*s->layerRole()))
                bars.push_back(s);

    return bars;
}

Int32 TopbarAutoHide::topbarsHeight() const noexcept
{
    Int32 height { 0 };

    for (LSurface *bar : topbars())
        height = std::max(height, bar->size().height() + std::max(0, bar->layerRole()->margins().top));

    return height;
}

bool TopbarAutoHide::topbarMenuOpen() const noexcept
{
    for (LSurface *bar : topbars())
        for (LPopupRole *popup : bar->layerRole()->childPopups())
            if (popup->surface()->mapped())
                return true;

    return false;
}

Int32 TopbarAutoHide::blockHeight() const noexcept
{
    const ToplevelRole *fullscreen { m_fullscreen.get() };
    return topbarsHeight() + (fullscreen && fullscreen->ssd() ? SSDTheme::TitlebarHeight : 0);
}

Int32 TopbarAutoHide::slide() const noexcept
{
    return Int32(std::lround((1.0 - m_progress) * blockHeight()));
}

Int32 TopbarAutoHide::slideFor(Float64 progress, const ToplevelRole *fullscreen) const noexcept
{
    const Int32 block { topbarsHeight() + (fullscreen && fullscreen->ssd() ? SSDTheme::TitlebarHeight : 0) };
    return Int32(std::lround((1.0 - progress) * block));
}

const ToplevelRole *TopbarAutoHide::leaving() const noexcept
{
    const Workspaces *ws { m_output.workspaces.get() };
    return ws && ws->moving() && m_leavingProgress > 0.0 ? m_leaving.get() : nullptr;
}

ToplevelRole *TopbarAutoHide::findFullscreen() const noexcept
{
    // The fullscreen window of the workspace shown (see Workspaces)
    const Workspaces *ws { m_output.workspaces.get() };
    ToplevelRole *toplevel { ws ? ws->currentFullscreen() : nullptr };
    return toplevel && !toplevel->isMinimized() && toplevel->surface()->mapped() ? toplevel : nullptr;
}

void TopbarAutoHide::update() noexcept
{
    ToplevelRole *fullscreen { findFullscreen() };

    if (fullscreen != m_fullscreen.get())
    {
        ToplevelRole *prev { m_fullscreen.get() };
        m_fullscreen.reset(fullscreen);

        // Left with the bars revealed: they stay there while it slides away (see leaving())
        m_leaving.reset(prev && m_progress > 0.0 ? prev : nullptr);
        m_leavingProgress = m_progress;

        // Shown on the desktop, hidden (until hovered) in a fullscreen workspace, right away: the
        // workspace slides in with them as they are there
        m_revealTimer.stop();
        setProgressNow(!fullscreen);

        // Their title bars follow (see FullscreenTitlebarY())
        if (prev)
            prev->updateDecorations();

        if (fullscreen)
            fullscreen->updateDecorations();
    }

    updateMirrors();

    // The workspace left stopped sliding: its title bar is hidden again
    if (m_leaving && !leaving())
    {
        ToplevelRole *left { m_leaving.get() };
        m_leaving.reset();

        if (left != m_fullscreen.get())
            left->updateDecorations();
    }

    if (!fullscreen)
    {
        m_revealTimer.stop();
        return;
    }

    const LOutput *cursorOutput { cursor()->output() };
    const SkScalar y { cursor()->pos().y() - SkScalar(m_output.pos().y()) };
    const bool onOutput { cursorOutput == &m_output };

    if (m_shown)
    {
        m_revealTimer.stop();

        // Kept while the cursor is over the bars or the title bar, or one of their menus is open.
        const Int32 zone { topbarsHeight() + (fullscreen->ssd() ? SSDTheme::TitlebarHeight : 0) };
        const bool keep { topbarMenuOpen() || SSD::ButtonPressed() || (onOutput && y < SkScalar(zone)) };

        if (!keep)
            setShown(false);

        return;
    }

    // Revealed by holding the cursor at the top edge (see reveal())
    if (!cursorAtEdge())
        m_revealTimer.stop();
    else if (!m_revealTimer.running())
        m_revealTimer.start(RevealDelayMs);
}

bool TopbarAutoHide::cursorAtEdge() const noexcept
{
    // Not while a client confines the cursor (e.g. a game)
    LSurface *focus { seat()->pointer()->focus() };

    return cursor()->output() == &m_output &&
           cursor()->pos().y() - SkScalar(m_output.pos().y()) < 1.f &&
           !(focus && focus->pointerConstraintEnabled());
}

void TopbarAutoHide::reveal() noexcept
{
    // Still held there
    if (m_fullscreen.get() && cursorAtEdge())
        setShown(true);
}

void TopbarAutoHide::setShown(bool shown) noexcept
{
    if (m_shown == shown)
        return;

    m_shown = shown;
    m_from = m_progress;
    m_to = shown ? 1.0 : 0.0;
    m_animation.reset(); // (Re)starts it from the current progress
}

void TopbarAutoHide::setProgressNow(bool shown) noexcept
{
    m_animation.pause();
    m_shown = shown;
    m_from = m_to = m_progress = shown ? 1.0 : 0.0;
    applyProgress();
}

void TopbarAutoHide::updateMirrors() noexcept
{
    const auto bars { topbars() };

    // Gone, or no longer a bar of this output
    std::erase_if(m_mirrors, [&bars](const auto &m){
        return !m->surface || std::find(bars.begin(), bars.end(), m->surface.get()) == bars.end();
    });

    for (LSurface *bar : bars)
    {
        const bool found { std::any_of(m_mirrors.begin(), m_mirrors.end(), [bar](const auto &m){ return m->surface.get() == bar; }) };

        if (found)
            continue;

        auto &m { m_mirrors.emplace_back(std::make_unique<Mirror>()) };
        m->surface.reset(bar);
        const SkRegion empty;
        m->node.setInputRegion(&empty);
        m->node.enableAutoDamage(false);
        m->node.setSrcRectMode(AKImage::SrcRectMode::Custom);
        m->node.layout().setPositionType(YGPositionTypeAbsolute);
        m->node.setVisible(false);
        m->node.setParent(&GetScene()->shell);
    }
}

void TopbarAutoHide::SyncMirrors(LOutput *rendering) noexcept
{
    for (LOutput *output : compositor()->outputs())
    {
        TopbarAutoHide *autoHide { AutoHideOf(output) };

        if (!autoHide)
            continue;

        // Where the bars are shown but not with their own view (it's in the workspace the user is in):
        // - A fullscreen workspace left with them revealed, while it slides away (as they were).
        // - Otherwise the desktop, while sliding away from a fullscreen workspace.
        const Workspaces *ws { static_cast<Output*>(output)->workspaces.get() };
        size_t index { 0 };
        Int32 dy { 0 };
        bool show { false };

        if (ws)
        {
            if (const ToplevelRole *left { autoHide->leaving() }; left && Workspaces::Find(left->surface(), &index) == ws)
            {
                dy = -autoHide->slideFor(autoHide->m_leavingProgress, left);
                show = true;
            }
            else if (ws->current() != 0)
            {
                index = 0;
                show = true;
            }

            show = show && std::abs(Float64(index) - ws->pos()) < 0.999 && rendering == output;
        }

        for (auto &m : autoHide->m_mirrors)
        {
            auto *bar { static_cast<Surface*>(m->surface.get()) };

            if (!show || !bar || !bar->mapped() || !bar->image() || !bar->layerRole())
            {
                m->node.setVisible(false);
                continue;
            }

            const SkIPoint pos { static_cast<LayerRole*>(bar->layerRole())->baseRolePos() + SkIPoint::Make(ws->slideX(index), dy) };
            m->node.setImage(bar->image());
            m->node.setCustomSrcRect(bar->srcRect());
            m->node.setCustomSrcRectScale(bar->scale());
            m->node.setSrcTransform(bar->bufferTransform());
            m->node.layout().setPosition(YGEdgeLeft, pos.x());
            m->node.layout().setPosition(YGEdgeTop, pos.y());
            m->node.layout().setWidth(bar->size().width());
            m->node.layout().setHeight(bar->size().height());
            m->node.setVisible(true);

            // Its blur, like the bar's own (see SurfaceBlurManager)
            auto *blur { static_cast<SurfaceBlurManager*>(bar->backgroundBlur()) };
            const bool blurred { blur && blur->visible() };

            if (blurred)
            {
                m->blur.setColorScheme(blur->props().colorScheme);
                m->blur.setRegion(blur->props().region);

                switch (blur->props().maskType)
                {
                case LBackgroundBlur::RoundRect:
                    m->blur.setRoundRectClip(blur->props().roundRectMask);
                    break;
                case LBackgroundBlur::SVGPath:
                    if (!blur->props().svgPathMask.isEmpty())
                    {
                        m->blur.setPathClip(blur->props().svgPathMask);
                        break;
                    }
                    [[fallthrough]];
                default:
                    m->blur.clearClip();
                }
            }

            if (blurred != m->blurAdded)
            {
                m->blurAdded = blurred;

                if (blurred)
                    m->node.addBackgroundEffect(&m->blur);
                else
                    m->node.removeBackgroundEffect(&m->blur);
            }
        }
    }
}

void TopbarAutoHide::SurfaceDamaged(const LSurface *surface) noexcept
{
    for (LOutput *output : compositor()->outputs())
        if (TopbarAutoHide *autoHide { AutoHideOf(output) })
            for (auto &m : autoHide->m_mirrors)
                if (m->surface.get() == surface && m->node.visible())
                    m->node.addDamage(AK_IRECT_INF);
}

void TopbarAutoHide::applyProgress() noexcept
{
    for (LSurface *bar : topbars())
    {
        auto &view { static_cast<Surface*>(bar)->view };
        view.syncVisibility();
        view.syncPos();
    }

    if (ToplevelRole *fullscreen { m_fullscreen.get() })
        fullscreen->updateDecorations();

    m_output.repaint();
}
