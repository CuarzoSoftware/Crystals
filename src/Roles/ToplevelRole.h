#ifndef TOPLEVELROLE_H
#define TOPLEVELROLE_H

#include <Louvre/Roles/LToplevelRole.h>
#include <Core/Types.h>
#include <memory>
#include <chrono>

class SSD;

/**
 * Server-side decorations (SSD): toplevels get them unless they prefer drawing their own
 * (xdg-decoration). Then the surface position (LSurface::pos()) is the top-left of the whole window,
 * title bar included, and the content sits right below the title bar (rolePos()). Fullscreen
 * toplevels show no decorations.
 */
class ToplevelRole : public LToplevelRole
{
public:
    ToplevelRole(const void *params) noexcept;
    ~ToplevelRole();

    /// The decorations, if shown.
    SSD *ssd() const noexcept { return m_ssd.get(); }

    /// Creates/destroys/re-lays out the decorations (mode, state or size changed).
    void updateDecorations() noexcept;

    /// Height above the content taken by the decorations (0 without them).
    Int32 titlebarHeight() const noexcept;

    /// The title bar height once the pending configuration is applied (server-side decorations
    /// configured, not fullscreen): what sizes sent to the client must leave room for.
    Int32 pendingTitlebarHeight() const noexcept;

    /// Places it on its output once mapped: maximized at the top of the available area, fullscreen
    /// over the output, otherwise centered (title bar included) below the top bar.
    void placeMapped() noexcept;

    SkIPoint rolePos() const override;
    void configureRequest() override;
    void preferredDecorationModeChanged() override;
    void setMaximizedRequest() override;
    void unsetFullscreenRequest() override;
    void titleChanged() override;
    void minimizedChanged() override;
    void appIdChanged() override;
    void stateChanged(CZBitset<Changes> changes, const State &prev) override;

private:
    DecorationMode wantedDecorationMode() const noexcept;

    /// Sends the maximized size (the available area minus the title bar) if maximizing/maximized.
    void configureMaximizedSize() noexcept;
    void configureAvailableBounds() noexcept;

    /**
     * Shortly after appearing, some clients size themselves for the whole available area and only
     * then switch to server-side decorations, so the title bar pushes their content past the
     * bottom of the screen. While young (FitWindowMs), a decorated window that doesn't fit is
     * configured again to fit (and centered once it does).
     */
    void fitIfYoung() noexcept;
    std::chrono::steady_clock::time_point m_created { std::chrono::steady_clock::now() };
    UInt32 m_fitAttempts { 0 };
    bool m_fitPending { false }; // Configured to fit: place it again once the new size arrives
    void enforceMinSize() noexcept;
    bool m_minSizeRequested { false };
    std::unique_ptr<SSD> m_ssd;
};

#endif // TOPLEVELROLE_H
