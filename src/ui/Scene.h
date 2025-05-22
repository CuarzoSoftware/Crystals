#ifndef SCENE_H
#define SCENE_H

#include <core/Global.h>
#include <AK/AKScene.h>
#include <AK/nodes/AKContainer.h>
#include <LLayout.h>
#include <array>

class Scene final : public AKScene
{
public:

    enum Layer
    {
        // TODO: Add custom layers
        Background  = LLayerBackground,
        Bottom      = LLayerBottom,
        Middle      = LLayerMiddle,
        Top         = LLayerTop,
        Overlay     = LLayerOverlay,
        LayerLast
    };

    Scene() noexcept;
    AKContainer root { YGFlexDirectionRow, false };
    std::array<AKContainer, LayerLast> layers;
};

#endif // SCENE_H
