#ifndef OUTPUT_H
#define OUTPUT_H

#include <LOutput.h>
#include <AKTarget.h>
#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Scene/Scene.h>
#include <Nodes/AKSolidColor.h>
#include <Nodes/AKText.h>
#include <Nodes/AKButton.h>
#include <Nodes/AKWindowButtonGroup.h>
#include <Nodes/AKTextField.h>
#include <Effects/AKBackgroundBlurEffect.h>

class blur : public AKBackgroundBlurEffect
{
public:
    using AKBackgroundBlurEffect::AKBackgroundBlurEffect;

    void onSceneCalculatedRect() override {
        AKBackgroundBlurEffect::onSceneCalculatedRect();
    }
};


class Output final : public LOutput
{
public:
    class Resources final : public CZObject
    {
    public:
        Resources(Output &output) noexcept;
        Output &output;
        //std::array<AKContainer, Scene::LayerLast> layers;

        std::shared_ptr<AKTarget> target;
        bool ignoreKayRepaintCalls { false };

        //AKSolidColor testSolidColor { SK_ColorBLUE };
        //AKText testText { "Crystals" };

        AKTextField field {};

        AKContainer testBlurContainer {};
        blur blurFX { &testBlurContainer };

        AKButton button { "Hello world!" };
        AKWindowButtonGroup windowBtns { nullptr };
    };

    using LOutput::LOutput;

    void initializeGL() override;
    void paintGL() override;
    void moveGL() override;
    void resizeGL() override;
    void uninitializeGL() override;

    void syncSurfaceViews() noexcept;
    void handleSurfaceCallbacks() noexcept;
    void addCursorDamage() noexcept;

    std::unique_ptr<Resources> res;
};

#endif // OUTPUT_H
