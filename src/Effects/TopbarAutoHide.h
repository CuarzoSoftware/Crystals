#ifndef TOPBARAUTOHIDE_H
#define TOPBARAUTOHIDE_H

#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Core/CZTimer.h>
#include <Core/CZWeak.h>
#include <Core/CZLinearAnimation.h>
#include <vector>
#include <memory>
#include <AK/Nodes/AKImage.h>
#include <AK/Effects/AKBackgroundBlurEffect.h>

/**
 * @brief Hides the top bars (e.g. Firmament's) of an output showing a fullscreen workspace.
 *
 * The bars belong to the desktop workspace, where they're always shown, but are also drawn in
 * fullscreen workspaces (see Workspaces): there they start hidden and are revealed on hover. The
 * surface's own view is always in the workspace the user is in (so its menus show there); while
 * sliding from a fullscreen workspace, a mirror (the same buffer, with its own blur) draws them in
 * the desktop workspace.
 *
 * Like macOS: while an output shows a fullscreen workspace (see Workspaces), its top bars slide up out of the
 * screen. Holding the cursor at the top edge (RevealDelayMs) slides them back down, together with
 * the window's title bar if decorated (beneath the bars, over the content), and they slide up again
 * once the cursor leaves them (unless one of their menus is open).
 *
 * Top bars: layer surfaces anchored to the top (not the bottom) of the output with an exclusive zone,
 * in the top or overlay layer. They're moved through their role position (LayerRole::rolePos()), so
 * input follows what's shown; fully hidden they're parked far away and not drawn.
 *
 * State changes call UpdateAll() and pointer motion PointerMoved() (main thread).
 */
class TopbarAutoHide final : public CZObject
{
public:
    static constexpr UInt32 RevealDelayMs { 800 };
    static constexpr UInt32 SlideMs { 260 };

    TopbarAutoHide(Output &output) noexcept;
    ~TopbarAutoHide();

    /// Re-evaluates every output (a window (un)fullscreened, mapped, minimized, raised, ...).
    static void UpdateAll() noexcept;

    /// The cursor moved: reveal/hide the bars of the outputs it's on/left.
    static void PointerMoved() noexcept { UpdateAll(); }

    /// Whether @p layerRole is one of the top bars this hides.
    static bool IsTopbar(const LLayerRole &layerRole) noexcept;

    /// How far up @p layerRole is slid (0 if not hidden by any output).
    static Int32 TopbarOffset(const LLayerRole &layerRole) noexcept;

    /// Whether @p surface is a fully hidden top bar (not drawn).
    static bool HidesSurface(const LSurface *surface) noexcept;

    /**
     * @brief Where the title bar of the fullscreen @p toplevel goes, relative to its content's top.
     *
     * Right below the top bars while revealed, above the content (off-screen) while hidden.
     */
    static Int32 FullscreenTitlebarY(const ToplevelRole &toplevel) noexcept;

    /// Whether the title bar of the fullscreen @p toplevel is (partly) on screen.
    static bool FullscreenTitlebarShown(const ToplevelRole &toplevel) noexcept;

    /// Positions the mirrors of the bars in the desktop workspace (Output::paintGL(), before rendering).
    static void SyncMirrors(LOutput *rendering) noexcept;

    /// A bar committed: its mirrors are redrawn.
    static void SurfaceDamaged(const LSurface *surface) noexcept;

    /// The fullscreen toplevel hiding the bars of this output, if any.
    ToplevelRole *fullscreen() const noexcept { return m_fullscreen.get(); }

private:
    void update() noexcept;
    bool cursorAtEdge() const noexcept;
    void reveal() noexcept; // The cursor was held at the top edge long enough
    void setShown(bool shown) noexcept;
    void applyProgress() noexcept;
    void setProgressNow(bool shown) noexcept; // No animation (e.g. another workspace shown)
    void updateMirrors() noexcept;

    // A top bar drawn in the desktop workspace while sliding away from a fullscreen one
    struct Mirror
    {
        CZWeak<LSurface> surface;
        AKImage node;
        AKBackgroundBlurEffect blur;
        bool blurAdded { false };
    };
    std::vector<std::unique_ptr<Mirror>> m_mirrors;

    /// The top bars on this output.
    std::vector<LSurface*> topbars() const noexcept;

    /// Height of the top bars (the tallest, margins included).
    Int32 topbarsHeight() const noexcept;

    /// Whether a menu (popup) of some top bar is open.
    bool topbarMenuOpen() const noexcept;

    /// The bars and title bar slide together as one block of this height.
    Int32 blockHeight() const noexcept;

    /// How far up the block is slid (0: shown).
    Int32 slide() const noexcept;

    /// The same for a given progress and fullscreen window (title bar or not).
    Int32 slideFor(Float64 progress, const ToplevelRole *fullscreen) const noexcept;

    /// The fullscreen window whose workspace is being left with the bars (and its title bar)
    /// revealed: they stay as they were there until the slide ends.
    const ToplevelRole *leaving() const noexcept;

    /// The fullscreen toplevel to hide the bars for: the one of the workspace shown (see Workspaces).
    ToplevelRole *findFullscreen() const noexcept;

    Output &m_output;
    CZWeak<ToplevelRole> m_fullscreen;
    CZTimer m_revealTimer;
    CZLinearAnimation m_animation;
    Float64 m_progress { 1.0 }; // 1: shown, 0: hidden
    CZWeak<ToplevelRole> m_leaving;    // See leaving()
    Float64 m_leavingProgress { 0.0 }; // Its m_progress when left
    Float64 m_from { 1.0 }, m_to { 1.0 };
    bool m_shown { true };
};

#endif // TOPBARAUTOHIDE_H
