#ifndef OUTPUT_H
#define OUTPUT_H

#include <core/Global.h>
#include <LOutput.h>
#include <AK/AKObject.h>
#include <include/core/SkRegion.h>

class Output final : public LOutput
{
public:
    class Resources final : public AKObject
    {
    public:
        Resources(Output &output) noexcept;
        ~Resources();
        AKSceneTarget *sceneTarget;
        Output &output;

        SkRegion inDamage;
        SkRegion outDamage;
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
    void handleOutDamage() noexcept;

    std::unique_ptr<Resources> res;
};

#endif // OUTPUT_H
