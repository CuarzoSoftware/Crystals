#ifndef SCENE_H
#define SCENE_H

#include <AK/Nodes/AKContainer.h>
#include <AK/AKScene.h>
#include <Louvre/Layout/LSurfaceLayer.h>
#include <Louvre/Louvre.h>
#include <array>

using namespace CZ;

/**
 * @brief The compositor's node tree.
 *
 * Bottom to top, under the root:
 * - desktop: the desktop (Desk), below every other surface.
 * - backdrops: behind fullscreen workspaces (see Workspaces).
 * - layers: one per Louvre layer (background ... overlay), the surfaces of each.
 * - animations: windows (un)minimizing, flying to/from the dock.
 * - shell: the desktop shell's panels (docks, Firmament's top bar), above every other surface.
 * - top: above everything (drag & drop icons).
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
    AKContainer top;

    /// Where the view of a parentless surface goes (one of the containers above).
    AKContainer &containerFor(LSurface &surface) noexcept;

private:
    Scene() noexcept;
};

#endif // SCENE_H
