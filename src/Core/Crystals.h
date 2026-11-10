#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <Core/Types.h>
#include <Louvre/Louvre.h>
#include <Louvre/LCompositor.h>
#include <AK/AKApp.h>
#include <Heaven/Compositor/HNCompositor.h>

using namespace CZ;
using namespace CZ::HNCompositorAPI;

class Crystals final : public LCompositor
{
public:
    Crystals() noexcept;
    const std::vector<Client*> &clients() const noexcept { return (std::vector<Client*> &)LCompositor::clients(); };
    std::shared_ptr<AKApp> app;
    std::shared_ptr<Scene> scene;
    std::shared_ptr<HNCompositor> heaven;

    /// The layer surface scope of Desk (the desktop: background and icons).
    static constexpr const char *DeskScope { "cz-desk" };

    /// Whether @p surface is (part of) the desktop.
    static bool IsDesk(LSurface *surface) noexcept;

    /// The client of the desktop (Desk), if running.
    Client *deskClient() const noexcept;

    /**
     * @brief Tells Heaven which client's menus to show: the active toplevel's, or the desktop's when
     *        no toplevel is active (like Finder on macOS), or none.
     */
    void updateActiveHeavenClient() noexcept;

    /**
     * @brief The desktop was clicked: it becomes the active "app" (its menus show right away) and
     *        the active toplevel is deactivated, until a toplevel is activated again.
     */
    void activateDesk() noexcept;

    /// Whether the desktop is the active "app" (see activateDesk()).
    bool deskActive() const noexcept { return m_deskActive; }

    /// A toplevel was activated: the desktop is no longer the active "app".
    void deactivateDesk() noexcept { m_deskActive = false; }

private:
    bool m_deskActive { false };
    void initialized() noexcept override;
    void uninitialized() noexcept override;
    LFactoryObject *createObjectRequest(LFactoryObject::Type objectType, const void *params) override;
};

#endif // COMPOSITOR_H
