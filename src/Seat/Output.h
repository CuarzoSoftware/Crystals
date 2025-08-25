#ifndef OUTPUT_H
#define OUTPUT_H

#include <LOutput.h>
#include <CZObject.h>
#include <AKTarget.h>
#include <Core/Types.h>

#include <Nodes/AKSolidColor.h>

class Output final : public LOutput
{
public:
    class Resources final : public CZObject
    {
    public:
        Resources(Output &output) noexcept;
        Output &output;
        std::shared_ptr<AKTarget> target;
        bool ignoreKayRepaintCalls { false };

        AKSolidColor testSolidColor { SK_ColorBLUE };
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
