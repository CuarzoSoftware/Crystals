#ifndef LAYERROLE_H
#define LAYERROLE_H

#include <Louvre/Roles/LLayerRole.h>
#include <Core/Types.h>

/// Layer surfaces: top bars slide up over fullscreen windows (see TopbarAutoHide).
class LayerRole final : public LLayerRole
{
public:
    using LLayerRole::LLayerRole;
    SkIPoint rolePos() const override;

    /// Where Louvre places it (without sliding it away, see rolePos()).
    SkIPoint baseRolePos() const noexcept { return LLayerRole::rolePos(); }
    void stateChanged(CZBitset<Changes> changes, const State &prev) noexcept override;

    /// Louvre's default, remembering the size sent (see settled()).
    void configureRequest() override;

    /// Whether the client committed the size it was last configured with (see ResizeBlackout).
    bool settled() const noexcept;

private:
    SkISize m_configuredSize {};
};

#endif // LAYERROLE_H
