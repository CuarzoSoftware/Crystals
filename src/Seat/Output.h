#ifndef OUTPUT_H
#define OUTPUT_H

#include <Louvre/Seat/LOutput.h>
#include <AK/AKTarget.h>
#include <Core/Types.h>
#include <Core/CZObject.h>
#include <Scene/Scene.h>
#include <Core/CZSpringAnimation.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Workspaces.h>
#include <Effects/ResizeBlackout.h>
#include <memory>

class Output final : public LOutput
{
public:
    class Resources final : public CZObject
    {
    public:
        Resources(Output &output) noexcept;
        Output &output;

        std::array<AKContainer, Scene::LayerLast> layers;

        std::shared_ptr<AKTarget> target;
        bool ignoreKayRepaintCalls { false };
    };

    using LOutput::LOutput;

    void initializeGL() override;
    void paintGL() override;
    void moveGL() override;
    void resizeGL() override;

    /// Its windows stay inside it (see ToplevelRole::keepReachable()).
    void availableGeometryChanged() override;

    /// An output configuration is being applied (see Seat::configureOutputsRequest()): windows are
    /// handled once it's done, and outputs are left where it puts them.
    static inline bool Configuring { false };

    /// The outputs' last rects become the current ones (see resizeGL(): not rearranged then).
    static void SyncLastRects() noexcept;
    void uninitializeGL() override;

    void initLayers() noexcept;
    void updateLayers() noexcept;
    void syncSurfaceViews() noexcept;
    void handleSurfaceCallbacks() noexcept;
    void updateCapturedBlurs() noexcept;

    std::unique_ptr<Resources> res;

    /// Hides the top bars while showing a fullscreen window (created by TopbarAutoHide::UpdateAll()).
    std::unique_ptr<TopbarAutoHide> autoHide;

    /// Its workspaces (created by Workspaces::Of()).
    std::unique_ptr<Workspaces> workspaces;

    /// Black while its shell catches up with a new size (see resizeGL()).
    std::unique_ptr<ResizeBlackout> blackout;
private:
    SkIRect m_lastRect {}; // See resizeGL()
    CZTransform m_lastTransform { CZTransform::Normal }; // See paintGL()
};

#endif // OUTPUT_H
