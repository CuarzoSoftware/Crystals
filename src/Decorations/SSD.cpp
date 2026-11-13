#include <Decorations/SSD.h>
#include <Decorations/SSDTheme.h>
#include <Roles/ToplevelRole.h>
#include <Roles/Surface.h>
#include <Scene/SurfaceView.h>
#include <Core/Crystals.h>
#include <AK/AKApp.h>
#include <AK/AKTheme.h>
#include <Core/CZTime.h>
#include <Louvre/Seat/LSeat.h>
#include <Louvre/Seat/LKeyboard.h>
#include <Louvre/Seat/LOutput.h>
#include <Louvre/Cursor/LCursor.h>
#include <Louvre/Roles/LToplevelMoveSession.h>
#include <Louvre/Roles/LToplevelResizeSession.h>
#include <Louvre/LCompositor.h>
#include <CZ/skia/modules/skparagraph/include/ParagraphBuilder.h>
#include <linux/input-event-codes.h>
#include <algorithm>
#include <vector>

using namespace SSDTheme;

// The button being pressed (clicked if released over it) and its window.
static SSD *s_pressedSSD { nullptr };
static AKWindowButton *s_pressedButton { nullptr };
static bool s_pressedInside { false };

// The window whose buttons are hovered.
static SSD *s_hoveredSSD { nullptr };

static SurfaceView &ViewOf(ToplevelRole &toplevel) noexcept
{
    return static_cast<Surface*>(toplevel.surface())->view;
}

static SkIPoint CursorPos() noexcept
{
    return SkIPoint::Make(Int32(CZ::cursor()->pos().x()), Int32(CZ::cursor()->pos().y()));
}

/* ------------------------------------------ SSD ------------------------------------------ */

SSD::SSD(ToplevelRole &toplevel) noexcept :
    m_toplevel(toplevel),
    m_titlebar(TitlebarActive.resolve(GetCompositor()->colorScheme()), CZBorderRadius::MakeTB(BorderRadius, 0), &m_frame)
{
    SurfaceView &view { ViewOf(toplevel) };
    const SkRegion empty;

    // The shadow behind everything of the window, the frame (title bar, resize areas) above the
    // content but below its popups.
    m_shadow.insertBefore(&view.below);
    m_frame.insertBefore(&view.popups);

    m_titlebar.layout().setPositionType(YGPositionTypeAbsolute);
    m_titlebar.layout().setPosition(YGEdgeLeft, ResizeOutset);
    m_titlebar.layout().setPosition(YGEdgeTop, ResizeOutset);
    m_titlebar.layout().setHeight(TitlebarHeight);
    m_titlebar.layout().setFlexDirection(YGFlexDirectionRow);
    m_titlebar.layout().setAlignItems(YGAlignCenter);

    // Input regions: the title bar, the button group and each button take their rect (set in
    // update()); the separator and title none (they're part of the title bar).
    m_separator.setInputRegion(&empty);
    m_title.setInputRegion(&empty);

    m_separator.setColor(Separator);
    m_separator.layout().setPositionType(YGPositionTypeAbsolute);
    m_separator.layout().setPosition(YGEdgeLeft, 0.f);
    m_separator.layout().setPosition(YGEdgeBottom, 0.f);
    m_separator.layout().setWidthPercent(100.f);
    m_separator.layout().setHeight(1.f);

    m_buttons.layout().setPositionType(YGPositionTypeAbsolute);
    m_buttons.layout().setPosition(YGEdgeLeft, ButtonsInset);
    m_buttons.layout().setPosition(YGEdgeTop, (TitlebarHeight - AKTheme::WindowButtonSize.height()) / 2.f);
    m_buttons.layout().setGap(YGGutterAll, AKTheme::WindowButtonGap);
    m_buttons.layout().setHeight(AKTheme::WindowButtonSize.height());

    // The title, centered, kept clear of the buttons (ellipsized if too long, see updateTitle()).
    m_titleBox.setInputRegion(&empty);
    m_titleBox.layout().setFlex(1.f);
    m_titleBox.layout().setMargin(YGEdgeHorizontal, TitleSideInset);
    m_titleBox.layout().setJustifyContent(YGJustifyCenter);
    m_titleBox.layout().setAlignItems(YGAlignCenter);
    m_title.setFontSize(TitleFontSize);
    m_title.setFontStyle(SkFontStyle(SkFontStyle::kSemiBold_Weight, SkFontStyle::kNormal_Width, SkFontStyle::kUpright_Slant));

    // Resize areas: corners first (they take the corner radius too, easier to grab), then edges.
    const CZBitset<CZEdge> edges[8] {
        CZEdgeTop | CZEdgeLeft, CZEdgeTop | CZEdgeRight, CZEdgeBottom | CZEdgeRight, CZEdgeBottom | CZEdgeLeft,
        CZEdgeTop, CZEdgeRight, CZEdgeBottom, CZEdgeLeft };

    for (size_t i = 0; i < m_resizeAreas.size(); i++)
        m_resizeAreas[i] = new ResizeArea(edges[i], &m_frame);

    m_titlebar.setCursor(CZCursorShape::Default);

    m_fullTitle = toplevel.title();
    update();
}

SSD::~SSD()
{
    if (s_pressedSSD == this)
    {
        s_pressedSSD = nullptr;
        s_pressedButton = nullptr;
    }

    if (s_hoveredSSD == this)
        s_hoveredSSD = nullptr;

    for (ResizeArea *area : m_resizeAreas)
        delete area;

    m_shadow.setParent(nullptr);
    m_frame.setParent(nullptr);
}

SkISize SSD::MinSize() noexcept
{
    // The 9-patch corners span (border radius + shadow radius) each side of the window frame.
    const Int32 corners { 2 * (BorderRadius + std::max(ShadowActiveRadius, ShadowInactiveRadius)) };
    return { corners, corners - TitlebarHeight };
}

bool SSD::IsDecoration(const AKNode *node) noexcept
{
    for (; node; node = node->parent())
        if (dynamic_cast<const Frame*>(node))
            return true;

    return false;
}

void SSD::setTitle(const std::string &title) noexcept
{
    if (m_fullTitle == title)
        return;

    m_fullTitle = title;
    m_titleWidth = -1;
    updateTitle();
}

void SSD::update() noexcept
{
    const SkISize size { m_toplevel.surface()->size() };
    m_active = m_toplevel.isActivated();

    // The frame: the title bar and the content plus the resize margin around them.
    const Int32 fw { size.width() + 2 * ResizeOutset };
    const Int32 fh { size.height() + TitlebarHeight + 2 * ResizeOutset };
    m_frame.layout().setPosition(YGEdgeLeft, -ResizeOutset);
    m_frame.layout().setPosition(YGEdgeTop, -TitlebarHeight - ResizeOutset);
    m_frame.layout().setWidth(fw);
    m_frame.layout().setHeight(fh);

    const auto rects { resizeRects() };
    const bool canResize { resizable() };

    for (size_t i = 0; i < m_resizeAreas.size(); i++)
    {
        auto &l { m_resizeAreas[i]->layout() };
        l.setPosition(YGEdgeLeft, rects[i].x());
        l.setPosition(YGEdgeTop, rects[i].y());
        l.setWidth(rects[i].width());
        l.setHeight(rects[i].height());
        m_resizeAreas[i]->setVisible(canResize);

        const SkRegion region { SkIRect::MakeWH(rects[i].width(), rects[i].height()) };
        m_resizeAreas[i]->setInputRegion(&region);
    }

    // The title bar, the button group and each button: their whole rect.
    const SkISize b { AKTheme::WindowButtonSize };
    const SkRegion titlebarRegion { SkIRect::MakeWH(size.width(), TitlebarHeight) };
    const SkRegion buttonsRegion { SkIRect::MakeWH(3 * b.width() + 2 * Int32(AKTheme::WindowButtonGap), b.height()) };
    const SkRegion buttonRegion { SkIRect::MakeWH(b.width(), b.height()) };
    m_titlebar.setInputRegion(&titlebarRegion);
    m_buttons.setInputRegion(&buttonsRegion);

    for (AKWindowButton *button : { &m_close, &m_minimize, &m_maximize })
        button->setInputRegion(&buttonRegion);

    m_titlebar.layout().setWidth(size.width());
    // Light or dark, like the rest of the desktop (see Crystals::colorScheme())
    const CZColorScheme scheme { GetCompositor()->colorScheme() };
    m_titlebar.setBackgroundColor((m_active ? TitlebarActive : TitlebarInactive).resolve(scheme));
    m_title.setTextColor(CZAdaptiveColor((m_active ? TitleActive : TitleInactive).resolve(scheme)));
    m_separator.setColor(CZAdaptiveColor(Separator.resolve(scheme)));
    m_shadow.setDark(scheme == CZColorScheme::Dark);
    updateButtons();
    updateTitle();

    // The shadow around the whole frame (title bar + content), offset down. Hidden while the window
    // is smaller than its corners (a client ignoring the minimum size), they'd overlap.
    const ShadowMargins sm { Margins(m_active) };
    const SkISize min { MinSize() };
    m_shadow.setVisible(size.width() >= min.width() && size.height() >= min.height());
    m_shadow.setActive(m_active);
    m_shadow.layout().setPosition(YGEdgeLeft, -sm.left);
    m_shadow.layout().setPosition(YGEdgeTop, -TitlebarHeight - sm.top);
    m_shadow.layout().setWidth(size.width() + sm.left + sm.right);
    m_shadow.layout().setHeight(size.height() + TitlebarHeight + sm.top + sm.bottom);
}

std::array<SkIRect, 8> SSD::resizeRects() const noexcept
{
    // Frame: the title bar and the content plus the resize margin around them.
    // Corners: the margin plus the corner radius inwards. Edges: the margin, between the corners.
    const SkISize size { m_toplevel.surface()->size() };
    const Int32 fw { size.width() + 2 * ResizeOutset };
    const Int32 fh { size.height() + TitlebarHeight + 2 * ResizeOutset };
    const Int32 g { ResizeOutset + BorderRadius }, m { ResizeOutset };

    return {
        SkIRect::MakeXYWH(0, 0, g, g),
        SkIRect::MakeXYWH(fw - g, 0, g, g),
        SkIRect::MakeXYWH(fw - g, fh - g, g, g),
        SkIRect::MakeXYWH(0, fh - g, g, g),
        SkIRect::MakeXYWH(g, 0, std::max(0, fw - 2 * g), m),
        SkIRect::MakeXYWH(fw - m, g, m, std::max(0, fh - 2 * g)),
        SkIRect::MakeXYWH(g, fh - m, std::max(0, fw - 2 * g), m),
        SkIRect::MakeXYWH(0, g, m, std::max(0, fh - 2 * g)) };
}

bool SSD::resizable() const noexcept
{
    // Not while maximized: the client was told its exact size.
    return !m_toplevel.isMaximized() && !m_toplevel.isFullscreen();
}

/* ----------------------------------------- Title ----------------------------------------- */

static SkScalar TextWidth(const std::string &text, const skia::textlayout::TextStyle &style) noexcept
{
    skia::textlayout::ParagraphStyle paragraphStyle;
    paragraphStyle.setTextStyle(style);
    auto builder { skia::textlayout::ParagraphBuilder::make(paragraphStyle, AKApp::Get()->fontCollection()) };
    builder->pushStyle(style);
    builder->addText(text.c_str(), text.size());
    builder->pop();
    auto paragraph { builder->Build() };
    paragraph->layout(1e6f);
    return paragraph->getMaxIntrinsicWidth();
}

void SSD::updateTitle() noexcept
{
    const Int32 available { std::max(0, Int32(m_toplevel.surface()->size().width()) - 2 * TitleSideInset) };

    if (available == m_titleWidth)
        return;

    m_titleWidth = available;
    const auto &style { m_title.textStyle() };

    if (m_fullTitle.empty() || TextWidth(m_fullTitle, style) <= available)
    {
        m_title.setText(m_fullTitle);
        return;
    }

    // The longest prefix (cut at a code point, trailing spaces dropped) that fits with "…".
    std::vector<size_t> cuts;

    for (size_t i = 1; i < m_fullTitle.size(); i++)
        if ((UInt8(m_fullTitle[i]) & 0xC0) != 0x80)
            cuts.push_back(i);

    auto candidate = [this](size_t bytes)
    {
        std::string s { m_fullTitle.substr(0, bytes) };

        while (!s.empty() && s.back() == ' ')
            s.pop_back();

        return s + "…";
    };

    std::string best { "…" };
    size_t lo { 0 }, hi { cuts.size() };

    while (lo < hi)
    {
        const size_t mid { (lo + hi) / 2 };
        std::string s { candidate(cuts[mid]) };

        if (TextWidth(s, style) <= available)
        {
            best = std::move(s);
            lo = mid + 1;
        }
        else
            hi = mid;
    }

    m_title.setText(best);
}

/* ---------------------------------------- Actions ---------------------------------------- */

void SSD::focus() noexcept
{
    if (!m_toplevel.isActivated())
        m_toplevel.configureState(m_toplevel.pendingConfiguration().windowState | CZWinActivated);

    m_toplevel.surface()->raise();

    // Louvre takes the keyboard focus away on presses outside client surfaces.
    if (!seat()->keyboard()->grab())
        seat()->keyboard()->setFocus(m_toplevel.surface());

    // Back from the desktop (its menus shown): the window's app is the active one again.
    if (GetCompositor()->deskActive())
    {
        GetCompositor()->deactivateDesk();
        GetCompositor()->updateActiveHeavenClient();
    }
}

void SSD::toggleMaximized() noexcept
{
    if (m_toplevel.isMaximized())
        m_toplevel.unsetMaximizedRequest();
    else
        m_toplevel.setMaximizedRequest();
}

void SSD::buttonClicked(AKWindowButton::Type type) noexcept
{
    switch (type)
    {
    case AKWindowButton::Close:
        m_toplevel.close();
        break;
    case AKWindowButton::Minimize:
        setButtonsHover(false);
        m_toplevel.setMinimized(true);
        break;
    case AKWindowButton::Maximize:
        toggleMaximized();
        break;
    default:
        break;
    }
}

void SSD::titlebarPressed(const CZPointerButtonEvent &e) noexcept
{
    focus();

    // Double click: zoom (like macOS).
    const UInt32 now { CZTime::Ms() };

    if (m_lastTitlebarPressMs != 0 && now - m_lastTitlebarPressMs < 400)
    {
        m_lastTitlebarPressMs = 0;
        toggleMaximized();
        return;
    }

    m_lastTitlebarPressMs = now;

    // Drag: move, the title bar kept below the top of the output's available area (the top bar).
    if (m_toplevel.isFullscreen())
        return;

    if (m_toplevel.moveSession().start(e, CursorPos()))
    {
        if (LOutput *output { CZ::cursor()->output() })
            m_toplevel.moveSession().setConstraints({ CZEdgeDisabled, output->pos().y() + output->availableGeometry().y(),
                                                      CZEdgeDisabled, CZEdgeDisabled });
    }
}

void SSD::resizePressed(const CZPointerButtonEvent &e, CZBitset<CZEdge> edges) noexcept
{
    focus();

    if (m_toplevel.isFullscreen() || m_toplevel.isMaximized())
        return;

    // Never smaller than the decorations allow (nor than the client asks).
    const SkISize ssdMin { MinSize() }, clientMin { m_toplevel.minSize() };
    m_toplevel.resizeSession().setMinSize({ std::max(ssdMin.width(), clientMin.width()),
                                            std::max(ssdMin.height(), clientMin.height()) });

    // The title bar never goes under the top bar (the constraint applies to the window's top-left,
    // which includes the title bar, see ToplevelRole::rolePos()).
    if (LOutput *output { CZ::cursor()->output() })
        m_toplevel.resizeSession().setConstraints({ CZEdgeDisabled, output->pos().y() + output->availableGeometry().y(),
                                                    CZEdgeDisabled, CZEdgeDisabled });

    m_toplevel.resizeSession().start(e, edges, CursorPos());
}

/* ---------------------------------------- Buttons ---------------------------------------- */

void SSD::setButtonsHover(bool hover) noexcept
{
    if (m_buttonsHover == hover)
        return;

    m_buttonsHover = hover;
    updateButtons();
}

void SSD::updateButtons() noexcept
{
    // Like macOS: colored while the window is active, gray otherwise; hovering the group shows the
    // symbols (colored even if inactive); the pressed button looks pressed while the cursor is on it.
    for (AKWindowButton *b : { &m_close, &m_minimize, &m_maximize })
    {
        AKWindowButton::State state { m_active ? AKWindowButton::Normal : AKWindowButton::Disabled };

        if (b == s_pressedButton && s_pressedInside)
            state = AKWindowButton::Pressed;
        else if (m_buttonsHover)
            state = AKWindowButton::Hover;

        b->setState(state);
    }
}

/* ----------------------------------------- Input ----------------------------------------- */

SSD::Hit SSD::hitTest(SkIPoint pos) const noexcept
{
    Hit hit;
    const SkISize size { m_toplevel.surface()->size() };
    const SkIPoint content { m_toplevel.rolePos() };

    // In frame coordinates.
    const Int32 x { pos.x() - (content.x() - ResizeOutset) };
    const Int32 y { pos.y() - (content.y() - TitlebarHeight - ResizeOutset) };

    if (!SkIRect::MakeWH(size.width() + 2 * ResizeOutset, size.height() + TitlebarHeight + 2 * ResizeOutset).contains(x, y))
        return hit;

    // The resize areas are above the title bar (the corners overlap its rounded corners).
    if (resizable())
    {
        const auto rects { resizeRects() };

        for (size_t i = 0; i < rects.size(); i++)
        {
            if (rects[i].contains(x, y))
            {
                hit.ssd = const_cast<SSD*>(this);
                hit.part = Hit::Resize;
                hit.edges = m_resizeAreas[i]->edges;
                return hit;
            }
        }
    }

    // Title bar (the content below it is the client's).
    if (!SkIRect::MakeXYWH(ResizeOutset, ResizeOutset, size.width(), TitlebarHeight).contains(x, y))
        return hit;

    hit.ssd = const_cast<SSD*>(this);
    hit.part = Hit::Titlebar;

    // The button group: three buttons with gaps between them.
    const SkISize b { AKTheme::WindowButtonSize };
    const Int32 gap { Int32(AKTheme::WindowButtonGap) };
    const Int32 bx { x - ResizeOutset - ButtonsInset };
    const Int32 by { y - ResizeOutset - (TitlebarHeight - b.height()) / 2 };

    if (SkIRect::MakeWH(3 * b.width() + 2 * gap, b.height()).contains(bx, by))
    {
        hit.part = Hit::Buttons;
        AKWindowButton *buttons[] { const_cast<AKWindowButton*>(&m_close),
                                    const_cast<AKWindowButton*>(&m_minimize),
                                    const_cast<AKWindowButton*>(&m_maximize) };
        const Int32 i { bx / (b.width() + gap) };

        if (i < 3 && bx % (b.width() + gap) < b.width())
            hit.button = buttons[i];
    }

    return hit;
}

SSD::Hit SSD::HitAt(const SkPoint &pos) noexcept
{
    const SkIPoint p { Int32(SkScalarFloorToInt(pos.x())), Int32(SkScalarFloorToInt(pos.y())) };

    // The topmost surface Louvre would give the pointer to. Walking the stacking order top-down, the
    // first of it or some decorations containing the point wins.
    LSurface *surface { seat()->surfaceAt(p) };

    for (auto L = compositor()->layers().rbegin(); L != compositor()->layers().rend(); L++)
    {
        for (auto S = L->rbegin(); S != L->rend(); S++)
        {
            LSurface *s { *S };
            auto *toplevel { static_cast<ToplevelRole*>(s->toplevel()) };
            const bool decorated { toplevel && toplevel->ssd() && s->mapped() && !toplevel->isMinimized() };

            if (decorated)
            {
                const Hit hit { toplevel->ssd()->hitTest(p) };

                // Over its own content only its resize corners count (they reach inwards).
                if (hit.ssd && (s != surface || hit.part == Hit::Resize))
                    return hit;
            }

            if (s == surface)
                return {};
        }
    }

    return {};
}

static CZCursorShape ResizeCursor(CZBitset<CZEdge> e) noexcept
{
    if (e.hasAll(CZEdgeTop | CZEdgeLeft))     return CZCursorShape::ResizeTL;
    if (e.hasAll(CZEdgeTop | CZEdgeRight))    return CZCursorShape::ResizeTR;
    if (e.hasAll(CZEdgeBottom | CZEdgeLeft))  return CZCursorShape::ResizeBL;
    if (e.hasAll(CZEdgeBottom | CZEdgeRight)) return CZCursorShape::ResizeBR;
    if (e.has(CZEdgeTop))                     return CZCursorShape::ResizeT;
    if (e.has(CZEdgeBottom))                  return CZCursorShape::ResizeB;
    if (e.has(CZEdgeLeft))                    return CZCursorShape::ResizeL;
    return CZCursorShape::ResizeR;
}

std::optional<CZCursorShape> SSD::PointerMotion(const Hit &hit) noexcept
{
    // Hovering the button group (a button, or the gaps between them).
    SSD *hovered { hit.part == Hit::Buttons ? hit.ssd : nullptr };

    if (s_hoveredSSD != hovered)
    {
        if (s_hoveredSSD)
            s_hoveredSSD->setButtonsHover(false);

        s_hoveredSSD = hovered;

        if (hovered)
            hovered->setButtonsHover(true);
    }

    // A pressed button looks pressed only while the cursor is on it.
    if (s_pressedButton)
    {
        const bool inside { hit.button == s_pressedButton };

        if (inside != s_pressedInside)
        {
            s_pressedInside = inside;
            s_pressedSSD->updateButtons();
        }
    }

    if (!hit.ssd)
        return std::nullopt;

    if (hit.part == Hit::Resize)
        return ResizeCursor(hit.edges);

    return CZCursorShape::Default;
}

bool SSD::ButtonPressed() noexcept
{
    return s_pressedButton != nullptr;
}

bool SSD::PointerButton(const Hit &hit, const CZPointerButtonEvent &e) noexcept
{
    // Releasing a pressed button: clicked if still on it (wherever the cursor is, the release is ours).
    if (s_pressedButton && e.button == BTN_LEFT && !e.pressed)
    {
        SSD *ssd { s_pressedSSD };
        const auto type { s_pressedButton->type() };
        const bool clicked { hit.button == s_pressedButton };
        s_pressedSSD = nullptr;
        s_pressedButton = nullptr;
        s_pressedInside = false;
        ssd->updateButtons();

        if (clicked)
            ssd->buttonClicked(type); // May destroy it

        return true;
    }

    if (!hit.ssd)
        return false;

    // Everything else over the decorations is theirs (other buttons do nothing).
    if (e.button != BTN_LEFT || !e.pressed)
        return true;

    seat()->dismissPopups();

    switch (hit.part)
    {
    case Hit::Buttons:
        if (hit.button)
        {
            s_pressedSSD = hit.ssd;
            s_pressedButton = hit.button;
            s_pressedInside = true;
            hit.ssd->updateButtons();
        }
        break;
    case Hit::Titlebar:
        hit.ssd->titlebarPressed(e);
        break;
    case Hit::Resize:
        hit.ssd->resizePressed(e, hit.edges);
        break;
    default:
        break;
    }

    return true;
}

/* ---------------------------------------- Nodes ---------------------------------------- */

SSD::Shadow::Shadow() noexcept : AKNinePatch()
{
    SkRegion empty;
    setInputRegion(&empty);
    layout().setPositionType(YGPositionTypeAbsolute);

    // The window covers the middle: nothing to draw there.
    setRegionVisibility(C, false);
}

void SSD::Shadow::setActive(bool active) noexcept
{
    if (m_active == active)
        return;

    m_active = active;
    m_dirty = true;
    addChange(CHImage); // onSceneBegin() picks the asset
}

void SSD::Shadow::setDark(bool dark) noexcept
{
    if (m_dark == dark)
        return;

    m_dark = dark;
    m_dirty = true;
    addChange(CHImage);
}

void SSD::Shadow::onSceneBegin()
{
    if (m_dirty || changes().testAnyOf(CHLayoutScale))
    {
        m_dirty = false;
        const auto &asset { ShadowAsset(scale(), m_active, m_dark) };
        setImage(asset.image);
        setCenter(asset.center);
    }

    AKNinePatch::onSceneBegin();
}

SSD::ResizeArea::ResizeArea(CZBitset<CZEdge> edges, AKNode *parent) noexcept :
    AKContainer(YGFlexDirectionColumn, false, parent),
    edges(edges)
{
    layout().setPositionType(YGPositionTypeAbsolute);
    setCursor(ResizeCursor(edges));
}

SSD::Frame::Frame(SSD &ssd) noexcept : AKContainer(YGFlexDirectionColumn, false), ssd(ssd)
{
    layout().setPositionType(YGPositionTypeAbsolute);

    // Only its children (title bar, resize areas) take input.
    SkRegion empty;
    setInputRegion(&empty);
}
