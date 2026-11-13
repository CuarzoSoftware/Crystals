#ifndef SSD_H
#define SSD_H

#include <Core/Types.h>
#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKNinePatch.h>
#include <AK/Nodes/AKRoundSolidColor.h>
#include <AK/Nodes/AKSolidColor.h>
#include <AK/Nodes/AKText.h>
#include <AK/Nodes/AKWindowButton.h>
#include <Core/CZObject.h>
#include <Core/CZEdge.h>
#include <Core/CZBitset.h>
#include <Core/CZCursorShape.h>
#include <Core/Events/CZPointerButtonEvent.h>
#include <array>
#include <optional>
#include <string>

/**
 * @brief Server-side decorations of a toplevel: a macOS-style title bar (traffic lights + title),
 *        the frame's drop shadow and hairline border, and invisible resize areas around it.
 *
 * Its nodes live in the toplevel's SurfaceView, laid out around the content (the view's origin):
 * the title bar sits right above it (see ToplevelRole::rolePos()). The shadow and border come from
 * a single stretchable 9-patch asset (SSDTheme::ShadowAsset()); the content's bottom corners are
 * rounded by the view itself (SurfaceView::setBottomRadius()).
 *
 * Input: the nodes are passive, Pointer hit-tests the scene and calls PointerMotion() and
 * PointerButton() directly (synchronously, the events never reach the client):
 * - Title bar: a press activates the window, a drag moves it, a double click toggles maximized.
 * - Traffic lights: hovering the group shows their symbols, a click (press and release on the same
 *   button) closes, minimizes or toggles maximized.
 * - Resize areas (each corner and edge): a press starts resizing from that corner/edge.
 */
class SSD final : public CZObject
{
public:
    SSD(ToplevelRole &toplevel) noexcept;
    ~SSD();

    /// Re-lays out the decorations around the content and applies the activation state.
    void update() noexcept;

    /// The full title (ellipsized to fit the title bar).
    void setTitle(const std::string &title) noexcept;

    /// Smallest content size: below it the shadow's corners would overlap.
    static SkISize MinSize() noexcept;

    /// Whether @p node is (inside) some window's decorations.
    static bool IsDecoration(const AKNode *node) noexcept;

    /// What of some window's decorations is at a point.
    struct Hit
    {
        enum Part { None, Titlebar, Buttons, Resize };
        SSD *ssd { nullptr };           ///< nullptr: no decorations there (or something above them)
        Part part { None };
        AKWindowButton *button { nullptr }; ///< Over the button group: the button, if not a gap
        CZBitset<CZEdge> edges;         ///< Resize: the corner/edge
    };

    /**
     * @brief The decorations at @p pos (global), if not covered by anything above them.
     *
     * Computed from the windows' geometry and Louvre's stacking order (the same Louvre uses to find
     * surfaces), not from the scene.
     */
    static Hit HitAt(const SkPoint &pos) noexcept;

    /**
     * @brief The pointer moved, now over @p hit.
     *
     * Updates the buttons' hover/pressed looks. Returns the cursor to show when over decorations.
     */
    static std::optional<CZCursorShape> PointerMotion(const Hit &hit) noexcept;

    /**
     * @brief A pointer button event with the cursor over @p hit.
     *
     * @return `true` if the decorations took it (it must not reach any surface).
     */
    static bool PointerButton(const Hit &hit, const CZPointerButtonEvent &e) noexcept;

    /// Whether a window button is held down (it holds the pointer until released).
    static bool ButtonPressed() noexcept;

private:
    class Shadow final : public AKNinePatch
    {
    public:
        Shadow() noexcept;
        void setActive(bool active) noexcept;
        void setDark(bool dark) noexcept;
    protected:
        void onSceneBegin() override;
    private:
        bool m_active { true }, m_dark { false }, m_dirty { true };
    };

    // A node that resizes the window from the given corner/edge.
    class ResizeArea final : public AKContainer
    {
    public:
        ResizeArea(CZBitset<CZEdge> edges, AKNode *parent) noexcept;
        const CZBitset<CZEdge> edges;
    };

    // The decorations' root. Holds the SSD so a hit node can be traced back to its window.
    class Frame final : public AKContainer
    {
    public:
        Frame(SSD &ssd) noexcept;
        SSD &ssd;
    };

    /// What of these decorations is at @p pos (global), ignoring stacking.
    Hit hitTest(SkIPoint pos) const noexcept;

    /// Resize areas in frame coordinates (corners then edges, see m_resizeAreas).
    std::array<SkIRect, 8> resizeRects() const noexcept;
    bool resizable() const noexcept;

    /// Activates the window and gives it the keyboard focus (title bar / resize presses).
    void focus() noexcept;
    void toggleMaximized() noexcept;
    void buttonClicked(AKWindowButton::Type type) noexcept;
    void titlebarPressed(const CZPointerButtonEvent &e) noexcept;
    void resizePressed(const CZPointerButtonEvent &e, CZBitset<CZEdge> edges) noexcept;

    void setButtonsHover(bool hover) noexcept;
    void updateButtons() noexcept;
    void updateTitle() noexcept;

    ToplevelRole &m_toplevel;
    bool m_active { true };
    bool m_buttonsHover { false };
    UInt32 m_lastTitlebarPressMs { 0 };

    std::string m_fullTitle;
    Int32 m_titleWidth { -1 }; // Width the shown title was fitted to

    Shadow m_shadow;
    Frame m_frame { *this };
    AKRoundSolidColor m_titlebar;
    AKSolidColor m_separator { CZAdaptiveColor(0), &m_titlebar };
    AKContainer m_buttons { YGFlexDirectionRow, false, &m_titlebar };
    AKWindowButton m_close { AKWindowButton::Close, &m_buttons };
    AKWindowButton m_minimize { AKWindowButton::Minimize, &m_buttons };
    AKWindowButton m_maximize { AKWindowButton::Maximize, &m_buttons };
    AKContainer m_titleBox { YGFlexDirectionRow, true, &m_titlebar };
    AKText m_title { "", &m_titleBox };

    // Above the title bar: the corners overlap its rounded corners.
    std::array<ResizeArea*, 8> m_resizeAreas {};
};

#endif // SSD_H
