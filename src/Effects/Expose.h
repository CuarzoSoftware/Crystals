#ifndef EXPOSE_H
#define EXPOSE_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZSpringAnimation.h>
#include <Core/CZLinearAnimation.h>
#include <list>
#include <unordered_map>
#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/Events/CZPointerButtonEvent.h>
#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKImage.h>
#include <AK/Nodes/AKSolidColor.h>
#include <AK/Nodes/AKOutline.h>
#include <AK/Effects/AKBackgroundBlurEffect.h>
#include <AK/Effects/AKEdgeShadow.h>
#include <AK/Effects/AKBackgroundBoxShadowEffect.h>
#include <AK/Nodes/AKRoundSolidColor.h>
#include <AK/Nodes/AKFontIcon.h>
#include <chrono>
#include <memory>
#include <vector>

/**
 * @brief Exposé (like macOS Mission Control): every window of the workspace shown, at once.
 *
 * Only on the output the cursor is on when it opens (output()): the others are left as they are
 * (their windows, top bars, docks, input).
 * On every output, the windows of each desktop workspace shrink and move so all of them fit without
 * overlapping (keeping roughly where they were). Fullscreen workspaces are shown as they are (their
 * window, dialogs and popups, live), only in the bar's thumbnails too. Meanwhile and a bar with a blurred background slides down
 * from the top with a thumbnail of each workspace (see Workspaces).
 *
 * - Opened by a three finger swipe up (following the fingers) or Ctrl+Super+Up.
 * - Closed by a swipe down, Ctrl+Super+Up/Down, Escape, or clicking: a window (activated), a
 *   workspace thumbnail or nowhere. A thumbnail grows from the bar until it fills the screen,
 *   becoming that workspace (switched to right away, hidden meanwhile, see Workspaces::jumpTo());
 *   the current one's just closes it.
 * - Hovering a window outlines it (its geometry, not client-drawn shadows) and shows a close button
 *   on its corner (asks it to close); dragging it onto another desktop's thumbnail moves it there
 *   (elsewhere: back where it was).
 * - A sideways three finger swipe (or Ctrl+Super+Left/Right) switches workspaces meanwhile: each
 *   workspace's windows are laid out on their own and slide with it (the bar stays, its outline
 *   follows the current one).
 * - Fullscreen workspaces' thumbnails can be dragged along the bar to reorder them (the desktop
 *   stays first); a click (no drag) grows into the workspace.
 * - A plus button on the right of the bar adds a desktop workspace: hovering it peeks the new
 *   desktop in from the edge, a click grows it into the last slot (the others making room).
 * - Hovering a workspace's thumbnail (not the first desktop's) shows a close button on its corner: the workspace
 *   is removed, its window leaving fullscreen (back to the desktop). The desktop can't be removed.
 * - The dock keeps working: it's shown while Exposé is (Shelf hides it again if it auto-hides),
 *   told through the session bus (org.cuarzo.Crystals ExposeChanged(b shown, s output name) on
 *   /org/cuarzo/Crystals, the dock of that output),
 *   and windows are laid out above it. The top bars fade out as the workspace bar comes in.
 *
 * The windows are shown through snapshots (see Snapshot), taken again whenever they draw, so they
 * stay live: the outputs keep repainting meanwhile and the hidden windows keep getting their frame
 * callbacks (see Output::paintGL()). The windows themselves are hidden meanwhile.
 *
 * Main thread, except the drawing (animation steps), as other effects.
 */
class Expose final : public CZObject
{
public:
    /// Swipe distance (touchpad units) to open/close it all the way.
    static constexpr Float64 SwipeDistance { 320.0 };

    /// Spring of the opening/closing (critically damped, see CZSpringAnimation), and following the
    /// fingers.
    static constexpr Float64 SpringStiffness { 13000.0 };
    static constexpr Float64 CloseStiffness { 19000.0 }; // Closing is a bit faster
    static constexpr Float64 SwipeFollowStiffness { 70000.0 };

    /// A workspace thumbnail growing until it fills the screen (faster than opening/closing).
    static constexpr Float64 ZoomStiffness { 22000.0 };

    /// Layout: space around/between windows, workspace bar thumbnails height (fraction of the
    /// output's) and padding.
    static constexpr Int32 WindowGap { 28 };
    static constexpr SkScalar ThumbHeight { 0.075f };
    static constexpr Int32 BarPadding { 22 };
    // The workspace bar's blur: Firmament's top bar material (Marco's MBlurMaterial::MenuBar, keep in sync)
    static constexpr SkScalar BarBlurRadius     { 24.f };
    static constexpr SkScalar BarBlurSaturation { 1.8f };
    static constexpr SkColor  BarBlurTintLight  { 0x99F6F6F6 };
    static constexpr SkColor  BarBlurTintDark   { 0x8C1E1E1E };
    static constexpr Int32 ThumbGap { 18 };
    static constexpr Int32 ThumbOutlineGap { 2 }; // Between a thumbnail and its outline

    /// Whether it's shown (opening, open or closing): input on its output goes to it (but the dock's).
    static bool Active() noexcept;

    /// The output it's shown on (while Active()).
    static Output *ShownOn() noexcept;

    /// How far up the top bars are moved, out of the way of the workspaces bar (with its progress).
    static Int32 TopbarOffset(const LLayerRole &layerRole) noexcept;

    /// Shadow below the workspaces bar: its height and opacity.
    static constexpr Int32 BarShadow { 8 };
    static constexpr SkScalar BarShadowOpacity { 0.55f };

    /// Close button on the hovered thumbnail's corner.
    static constexpr Int32 CloseSize { 28 };
    static constexpr Int32 CloseShadowPad { 8 }; // Room around it for its shadow's blur

    /// The new desktop peeking in while the plus button is hovered (fraction of a thumbnail shown),
    /// and its growing into the bar once clicked.
    static constexpr SkScalar PeekVisible { 0.45f };
    static constexpr UInt32 AddMs { 320 };

    /// The plus button: just the sign (no circle).
    static constexpr Int32 PlusIconSize { 34 };

    /// A window being dragged (to another desktop) is see-through.
    static constexpr SkScalar DraggedOpacity { 0.5f };

    /// A removed workspace's thumbnail shrinking and fading out.
    static constexpr UInt32 RemoveMs { 260 };

    /// Whether @p surface is hidden (a window shown through its snapshot).
    static bool HidesSurface(const LSurface *surface) noexcept;

    /// Whether frames of @p surface must keep being requested (its snapshot is redrawn).
    static bool WantsFrames(const LSurface *surface) noexcept { return HidesSurface(surface); }

    /// Opens it if closed (or closing), closes it otherwise.
    static void Toggle() noexcept;

    /// Ctrl+Super+Up/Down, Escape. Returns `true` if the key was taken.
    static bool KeyEvent(const CZKeyboardKeyEvent &e) noexcept;

    /// A vertical three finger swipe (see Pointer). Up opens, down closes.
    static void SwipeBegin() noexcept;
    static void SwipeUpdate(const SkPoint &delta) noexcept;
    static void SwipeEnd(bool cancelled) noexcept;

    /// The pointer moved / a button. Return `true` if taken (not over the dock).
    static bool PointerMove() noexcept;
    static bool PointerButton(const CZPointerButtonEvent &e) noexcept;

    /// A surface drew: its window's snapshot is taken again on its output's next frame.
    static void SurfaceDamaged(const LSurface *surface) noexcept;

    /// Takes the snapshots of the windows on @p output that drew (Output::paintGL(), before
    /// rendering; no main thread timer: those can be delayed while the outputs repaint).
    static void ProcessPending(Output &output) noexcept;

    /// Windows were mapped/unmapped/minimized: the layout is rebuilt.
    static void WindowsChanged() noexcept;

    /// Workspaces were removed: rebuilt on the next frame (see ProcessPending(); any thread).
    static void WorkspacesChanged() noexcept;

    /// Some output's workspaces slid (see Workspaces): the layouts slide with them. Only visual.
    static void WorkspacesMoved() noexcept;

    static void Clear() noexcept;

    ~Expose();

private:
    struct Window
    {
        CZWeak<ToplevelRole> toplevel;
        Output *output { nullptr };
        size_t workspace { 0 };
        bool laidOut { false };   // The desktop's: moves to a slot of the layout (sliding with it). Otherwise
                                  // (fullscreen workspaces) shown as is, only snapshotted for its thumbnail
        bool dirty { false };     // Drew since its last snapshot
        SkIRect margins {};       // Shadow room in the image
        SkRect from {}, slot {};  // Its rect on screen and in the layout (title bar + content)
        SkRect shown {};          // Where it's drawn now (see apply())
        SkRect geometry {};       // The window itself within `from` (relative): without client-drawn shadows
        AKImage node;             // Moving to its slot
        AKImage thumb;            // In its workspace's thumbnail
    };

    struct Thumb
    {
        size_t workspace { 0 };
        SkRect rect {};           // In the bar
        AKContainer box { YGFlexDirectionColumn, false };
        AKSolidColor fill { CZAdaptiveColor(SK_ColorBLACK) }; // Behind (fullscreen ones: black)
        AKImage desktop;          // The desktop picture (desktop workspace)
        AKOutline frame;          // Marks the current one (around it)
        AKImage topbar;           // The top bar (desktop workspace), above its windows, its blur baked in
        SkRect topbarFrom {};     // Where it is on the output
    };

    struct Screen
    {
        Output *output { nullptr };
        SkScalar barHeight { 0.f };
        AKContainer bar { YGFlexDirectionColumn, false };
        AKSolidColor barFill { CZAdaptiveColor(0) };
        AKBackgroundBlurEffect blur;
        AKEdgeShadow shadow { CZEdgeBottom }; // A thin one below the bar
        std::vector<std::unique_ptr<Thumb>> thumbs;

        // On the hovered thumbnail's corner (not the desktop's)
        AKRoundSolidColor closeButton { SK_ColorWHITE, CZBorderRadius::Make(CloseSize / 2) };
        AKFontIcon closeIcon { "close", UInt32(CloseSize * 0.65f), &closeButton };
        AKImage closeShadow;  // Behind it (a blurred circle, see CloseShadowPad)
        size_t hovered { SIZE_MAX };
        SkRect closeRect {};  // In the bar

        // Thumbnail slots (x in the bar, left to right) and their width (see dragTo())
        std::vector<SkScalar> slots;
        SkScalar thumbWidth { 0.f };
        SkScalar thumbHeight { 0.f };

        // The desktop's picture and top bar (for the desktops' thumbnails and the peek)
        std::shared_ptr<RImage> desktopImage, topbarImage;
        SkRect topbarFrom {};

        // Plus button (adds a desktop) and the new desktop peeking in from the right edge
        AKImage plusShadow;
        AKRoundSolidColor plusButton { SK_ColorWHITE, CZBorderRadius::Make(CloseSize / 2) };
        AKFontIcon plusIcon { "add", UInt32(PlusIconSize), &plusButton };
        SkRect plusRect {};   // In the bar
        AKContainer peek { YGFlexDirectionColumn, false };
        AKImage peekDesktop;
        AKImage peekTopbar;
        SkRect peekRect {};   // In the bar (where it peeks)
        bool plusHovered { false };
    };

    Expose() noexcept;
    static Expose *Get(bool create) noexcept;

    void activate() noexcept;
    void deactivate() noexcept;
    void build() noexcept;
    void layoutScreen(Screen &screen) noexcept;

    /// Places the windows of a thumbnail scaled by @p fit (thumbnail / output width).
    void placeThumbWindows(Screen &screen, Thumb &thumb, SkScalar fit) noexcept;

    /// Grows @p thumb from the bar to the whole output (see zoomStep()).
    void zoomInto(Screen &screen, Thumb &thumb) noexcept;
    void zoomStep(Float64 t) noexcept;
    void apply() noexcept;
    void animateTo(Float64 target, Float64 velocity) noexcept;
    void close(ToplevelRole *activate = nullptr) noexcept;
    void snapshot(Window &window) noexcept;

    /// Tells the dock (Shelf) whether Exposé is showing (once per change).
    void notify(bool shown) noexcept;
    bool m_notified { false };
    std::shared_ptr<CZBus> m_bus; // Kept: the connection closes with its last reference
    Window *windowAt(const SkPoint &pos) noexcept;
    Thumb *thumbAt(const SkPoint &pos, Screen **screen) noexcept;
    Screen *screenOf(const Output *output) noexcept;
    void setHovered(Screen &screen, size_t workspace) noexcept;

    // A thumbnail pressed: a click grows into it, a drag reorders it
    static constexpr SkScalar DragThreshold { 6.f };
    Screen *m_pressScreen { nullptr };
    Thumb *m_pressThumb { nullptr };
    SkPoint m_pressPos {};
    SkScalar m_grabX { 0.f };      // Pointer x inside the thumbnail when pressed
    bool m_dragging { false };
    size_t m_dropSlot { 0 };       // Where it'd go (slot index)
    void dragTo(const SkPoint &pos) noexcept;
    void endPress(bool released) noexcept;

    // Hovered window (outlined) and a window pressed: a click activates it, a drag moves it
    AKOutline m_windowOutline;
    AKImage m_windowCloseShadow;
    AKRoundSolidColor m_windowClose { SK_ColorWHITE, CZBorderRadius::Make(CloseSize / 2) };
    AKFontIcon m_windowCloseIcon { "close", UInt32(CloseSize * 0.65f), &m_windowClose };
    SkRect m_windowCloseRect {};   // Global
    SkRect windowGeometryShown(const Window &w) const noexcept;
    Window *m_hoverWindow { nullptr };
    Window *m_pressWindow { nullptr };
    SkPoint m_windowGrab {};       // Pointer inside it, as a fraction of its rect
    bool m_windowDragging { false };
    Thumb *m_dropThumb { nullptr }; // The desktop's thumbnail it'd go to
    void setHoverWindow(Window *window) noexcept;
    void placeWindowOutline() noexcept;
    void dragWindowTo(const SkPoint &pos) noexcept;
    void endWindowPress(bool released) noexcept;

    // Adding a desktop (see the plus button)
    void setPlusHovered(Screen &screen, bool hovered) noexcept;
    void addDesktop(Screen &screen) noexcept;
    void addStep(Float64 t) noexcept;
    Screen *m_addScreen { nullptr };
    std::vector<std::pair<Thumb*, std::pair<SkScalar, SkScalar>>> m_addMoves; // From/to x
    SkRect m_addFrom {}, m_addTo {};
    CZLinearAnimation m_addAnim;
    void placeThumb(Thumb &thumb, SkScalar x) noexcept;
    std::vector<Thumb*> shownThumbs(Screen &screen) noexcept;

    // A removed workspace's thumbnail (a copy: the bar is rebuilt), shrinking and fading out
    struct Removed
    {
        SkRect rect {};                      // Global, where it was
        AKContainer box { YGFlexDirectionColumn, false };
        AKSolidColor fill { CZAdaptiveColor(SK_ColorBLACK) };
        std::list<AKImage> images;           // Its windows (their snapshots kept)
        CZLinearAnimation anim;
        bool done { false };
    };
    std::list<std::unique_ptr<Removed>> m_removed;
    void animateRemoval(Screen &screen, Thumb &thumb) noexcept;
    bool m_rebuildPending { false };
    std::unordered_map<Int32, std::shared_ptr<RImage>> m_closeShadows; // By scale (see layoutScreen())

    std::vector<std::unique_ptr<Window>> m_windows; // Bottom to top

    // The laid out windows, clipped to the output (sliding workspaces would show them on the
    // outputs next to it): the inner container is offset back, so they're placed in global
    // coordinates. A window being dragged (or activated while closing) leaves it, above the bar.
    AKContainer m_windowsClip { YGFlexDirectionColumn, false };
    AKContainer m_windowsLayer { YGFlexDirectionColumn, false, &m_windowsClip };
    void placeWindowsClip() noexcept;
    void restackWindows() noexcept;
    std::vector<std::unique_ptr<Screen>> m_screens;
    bool m_active { false };
    Output *m_output { nullptr }; // Where it's shown (see ShownOn())
    bool m_open { false };          // Where it's going: open or closed
    Float64 m_progress { 0.0 };     // 0: closed, 1: open

    CZSpringAnimation m_spring;

    bool m_swiping { false };
    Float64 m_swipeRaw { 0.0 };
    Float64 m_swipeVelocity { 0.0 };
    std::chrono::steady_clock::time_point m_swipeLast;

    // A thumbnail growing into its workspace (its output's windows stay hidden meanwhile)
    CZSpringAnimation m_zoom;
    Screen *m_zoomScreen { nullptr };
    Thumb *m_zoomThumb { nullptr };
    SkRect m_zoomFrom {};
};

#endif // EXPOSE_H
