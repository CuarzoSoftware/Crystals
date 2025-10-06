#ifndef OUTPUT_H
#define OUTPUT_H

#include <LOutput.h>
#include <AKTarget.h>
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
        //std::array<AKContainer, Scene::LayerLast> layers;

        std::shared_ptr<AKTarget> target;
        bool ignoreKayRepaintCalls { false };
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
