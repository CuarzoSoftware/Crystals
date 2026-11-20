#ifndef SCENE_H
#define SCENE_H

#include <AK/Nodes/AKContainer.h>
#include <AK/Nodes/AKSolidColor.h>
#include <Core/CZLinearAnimation.h>
#include <AK/AKScene.h>
#include <Louvre/Layout/LSurfaceLayer.h>
#include <Louvre/Louvre.h>
#include <Core/CZWeak.h>
#include <array>
#include <vector>

using namespace CZ;

/**
 * @brief The compositor's node tree.
 *
 * Bottom to top, under the root:
 * - desktop: the desktop (Desk), below every other surface.
 * - backdrops: behind fullscreen workspaces (see Workspaces).
 * - layers: one per Louvre layer (background ... overlay), the surfaces of each.
 * - animations: windows (un)minimizing, flying to/from the dock.
 * - shell: the desktop shell's top bars (Firmament's), above every other surface.
 * - expose: Exposé (see Expose), above the top bars, below the docks and popups.
 * - docks: the desktop shell's other panels (Shelf's docks), above Exposé (they keep working there).
 * - popups: popups (menus, tooltips) of any surface, above everything else (nested popups inside
 *   their parent popup). Input matches (see Seat::surfaceAt()).
 * - top: above everything (drag & drop icons).
 * - blackouts: black over outputs that changed size until their shell caught up (see ResizeBlackout).
 * - lock: the session lock surfaces. While locked, every other container is hidden (see
 *   setLocked() and SessionLockManager).
 * - unlockFade: black over everything, fading out once unlocked.
 *
 * Louvre only knows its own layers: the shell and desktop surfaces are kept at the top of the
 * overlay layer and the bottom of the background layer (see Crystals::restack()), so input matches
 * what's shown. Child surfaces (popups, subsurfaces) are drawn with their parent.
 */
class Scene final : public CZObject
{
public:

    enum Layer
    {
        Background  = LLayerBackground,
        Bottom      = LLayerBottom,
        Middle      = LLayerMiddle,
        Top         = LLayerTop,
        Overlay     = LLayerOverlay,
        LayerLast
    };

    static std::shared_ptr<Scene> Make() noexcept { return std::shared_ptr<Scene>(new Scene()); }
    std::shared_ptr<AKScene> scene;
    AKContainer root { YGFlexDirectionRow, false };
    AKContainer desktop;
    AKContainer backdrops;
    std::array<AKContainer, LayerLast> layers;
    AKContainer animations;
    AKContainer shell;
    AKContainer expose;
    AKContainer docks;
    AKContainer popups;
    AKContainer top;
    AKContainer blackouts;
    AKContainer lock;
    AKSolidColor unlockFade { CZAdaptiveColor(SK_ColorBLACK) };

    /**
     * @brief Shows only the lock surfaces (over black), or everything again: the desktop then fades
     *        in from black (unlockFade).
     */
    void setLocked(bool locked) noexcept;
    bool locked() const noexcept { return m_locked; }

    /// Where the view of a parentless surface goes (one of the containers above).
    AKContainer &containerFor(LSurface &surface) noexcept;

private:
    Scene() noexcept;
    bool m_locked { false };
    std::vector<CZWeak<AKNode>> m_hiddenByLock;
    CZLinearAnimation m_unlockFadeAnim;
};

#endif // SCENE_H
