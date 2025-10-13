#ifndef OUTPUT_H
#define OUTPUT_H

#include <Louvre/Seat/LOutput.h>
#include <AK/AKTarget.h>
#include <AK/Nodes/AKText.h>
#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Scene/Scene.h>

class Output final : public LOutput
{
public:
    class Resources final : public CZObject
    {
    public:
        Resources(Output &output) noexcept;
        Output &output;

        std::array<AKContainer, Scene::LayerLast> layers;
        AKText welcome { "F1 or Fn + F1: Launch terminal.\nCtrl + Shift + Esc: Exit.", &layers[LLayerBackground] };

        std::shared_ptr<AKTarget> target;
        bool ignoreKayRepaintCalls { false };
    };

    using LOutput::LOutput;

    void initializeGL() override;
    void paintGL() override;
    void moveGL() override;
    void resizeGL() override;
    void uninitializeGL() override;

    void initLayers() noexcept;
    void updateLayers() noexcept;
    void syncSurfaceViews() noexcept;
    void handleSurfaceCallbacks() noexcept;

    std::unique_ptr<Resources> res;
};

#endif // OUTPUT_H
