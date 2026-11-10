#ifndef TOPLEVELROLE_H
#define TOPLEVELROLE_H

#include <Louvre/Roles/LToplevelRole.h>
#include <Core/Types.h>
#include <memory>

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

    SkIPoint rolePos() const override;
    void configureRequest() override;
    void preferredDecorationModeChanged() override;
    void setMaximizedRequest() override;
    void titleChanged() override;
    void minimizedChanged() override;
    void appIdChanged() override;
    void stateChanged(CZBitset<Changes> changes, const State &prev) override;

private:
    DecorationMode wantedDecorationMode() const noexcept;
    void enforceMinSize() noexcept;
    bool m_minSizeRequested { false };
    std::unique_ptr<SSD> m_ssd;
};

#endif // TOPLEVELROLE_H
