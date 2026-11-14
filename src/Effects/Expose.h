#ifndef EXPOSE_H
#define EXPOSE_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZWeak.h>
#include <Core/CZSpringAnimation.h>
#include <Core/Events/CZKeyboardKeyEvent.h>
#include <Core/Events/CZPointerButtonEvent.h>
#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKImage.h>
#include <AK/Nodes/AKSolidColor.h>
#include <AK/Nodes/AKOutline.h>
#include <AK/Effects/AKBackgroundBlurEffect.h>
#include <chrono>
#include <memory>
#include <vector>

/**
 * @brief Exposé (like macOS Mission Control): every window of the workspace shown, at once.
 *
 * On every output, the windows of the workspace shown shrink and move so all of them fit without
 * overlapping (keeping roughly where they were), and a bar with a blurred background slides down
 * from the top with a thumbnail of each workspace (see Workspaces).
 *
 * - Opened by a three finger swipe up (following the fingers) or Ctrl+Super+Up.
 * - Closed by a swipe down, Ctrl+Super+Up/Down, Escape, or clicking: a window (activated), a
 *   workspace thumbnail or nowhere. A thumbnail grows from the bar until it fills the screen,
 *   becoming that workspace (switched to right away, hidden meanwhile, see Workspaces::jumpTo()).
 * - A sideways three finger swipe (or Ctrl+Super+Left/Right) switches workspaces meanwhile: each
 *   workspace's windows are laid out on their own and slide with it (the bar stays, its outline
 *   follows the current one).
 * - The dock keeps working: it's shown while Exposé is (Shelf hides it again if it auto-hides),
 *   told through the session bus (org.cuarzo.Crystals ExposeChanged(b) on /org/cuarzo/Crystals),
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
    static constexpr SkScalar ThumbHeight { 0.09f };
    static constexpr Int32 BarPadding { 14 };
    static constexpr Int32 ThumbGap { 18 };
    static constexpr Int32 ThumbOutlineGap { 2 }; // Between a thumbnail and its outline

    /// Whether it's shown (opening, open or closing): input goes to it (but the dock's).
    static bool Active() noexcept;

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
        bool laidOut { false };   // Moves to a slot of its workspace's layout (sliding with it)
        bool dirty { false };     // Drew since its last snapshot
        SkIRect margins {};       // Shadow room in the image
        SkRect from {}, slot {};  // Its rect on screen and in the layout (title bar + content)
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
        std::vector<std::unique_ptr<Thumb>> thumbs;
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

    std::vector<std::unique_ptr<Window>> m_windows; // Bottom to top
    std::vector<std::unique_ptr<Screen>> m_screens;
    bool m_active { false };
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
