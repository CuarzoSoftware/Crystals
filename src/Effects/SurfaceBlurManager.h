#ifndef SURFACEBLURMANAGER_H
#define SURFACEBLURMANAGER_H

#include <AK/Effects/AKBackgroundBlurEffect.h>
#include <Louvre/Roles/LBackgroundBlur.h>
#include <Core/Types.h>

class SurfaceBlurManager final : public LBackgroundBlur
{
public:
    SurfaceBlurManager(const void *params) noexcept;
    void configureRequest() override;
    void propsChanged(CZBitset<PropChanges> changes, const Props &prevProps) override;

    AKBackgroundBlurEffect fx {};

    /**
     * @brief Applies the client's clip (mask) to the blur, intersected with the rounded bottom
     *        corners of the surface view, if any (server-side decorated windows, see
     *        SurfaceView::setBottomRadius()).
     *
     * Called when the mask, the radius or the surface size changes.
     */
    void updateClip() noexcept;

    /**
     * @brief Applies a client's material (lvr_background_blur v2) to @p fx, or the defaults
     *        (Apple-like, see AKBackgroundBlurEffect) without one, e.g. with ext_background_effect_v1.
     */
    static void ApplyMaterial(AKBackgroundBlurEffect &fx, const std::optional<Material> &material) noexcept;

    /**
     * @brief The compositor's own fade of the blur (e.g. Exposé hiding the top bar), multiplied by
     *        the client's (Props::opacity).
     */
    void setCompositorOpacity(SkScalar opacity) noexcept;

    /// Re-blurred while rendering (see Output::paintGL()): captures of its toplevel are redrawn.
    bool blurred { false };

private:
    SkScalar m_compositorOpacity { 1.f };
    void updateOpacity() noexcept { fx.setOpacity(props().opacity * m_compositorOpacity); }
};

#endif // SURFACEBLURMANAGER_H
