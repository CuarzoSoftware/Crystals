#ifndef SCENE_H
#define SCENE_H

#include <Nodes/AKContainer.h>
#include <AKScene.h>
#include <LLayout.h>
#include <array>

using namespace CZ;

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
    std::array<AKContainer, LayerLast> layers;

private:
    Scene() noexcept;
};

#endif // SCENE_H
