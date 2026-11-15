#include <CZ/skia/core/SkRegion.h>
#include <Effects/SurfaceBlurManager.h>
#include <Roles/Surface.h>
#include <LClient.h>
#include <CZ/skia/core/SkRRect.h>
#include <CZ/skia/pathops/SkPathOps.h>

SurfaceBlurManager::SurfaceBlurManager(const void *params) noexcept : LBackgroundBlur(params)
{
    fx.onBlurred.subscribe(this, [this]{ blurred = true; });
}

void SurfaceBlurManager::configureRequest()
{
    configureState(State::Enabled);
}

void SurfaceBlurManager::propsChanged(CZBitset<PropChanges> changes, const Props &)
{
    auto &surface { *static_cast<Surface*>(this->surface()) };

    if (changes.has(PropChanges::ColorSchemeChanged))
        fx.setColorScheme(props().colorScheme);

    if (changes.has(PropChanges::RegionChanged))
        fx.setRegion(props().region);

    if (changes.has(PropChanges::MaterialChanged))
        ApplyMaterial(fx, props().material);

    if (changes.has(PropChanges::OpacityChanged))
        updateOpacity();

    if (changes.has(PropChanges::MaskChanged))
        updateClip();

    if (changes.has(PropChanges::StateChanged))
    {
        if (props().state == Enabled)
            surface.view.view.addBackgroundEffect(&fx);
        else
            surface.view.view.removeBackgroundEffect(&fx);
    }
}

void SurfaceBlurManager::setCompositorOpacity(SkScalar opacity) noexcept
{
    m_compositorOpacity = std::clamp(opacity, 0.f, 1.f);
    updateOpacity();
}

void SurfaceBlurManager::ApplyMaterial(AKBackgroundBlurEffect &fx, const std::optional<Material> &material) noexcept
{
    if (!material)
    {
        fx.setBlurRadius(AKBackgroundBlurEffect::DefaultBlurRadius);
        fx.setMaterial(CZColorScheme::Light, AKBackgroundBlurEffect::DefaultLightMaterial);
        fx.setMaterial(CZColorScheme::Dark, AKBackgroundBlurEffect::DefaultDarkMaterial);
        return;
    }

    // The client picks it for its current color scheme (it sends a new one when that changes)
    const AKBackgroundBlurEffect::Material m { material->saturation, material->tint };
    fx.setBlurRadius(material->radius); // Clamped by the effect
    fx.setMaterial(CZColorScheme::Light, m);
    fx.setMaterial(CZColorScheme::Dark, m);
}

static SkRRect ToSkRRect(const CZRRect &r) noexcept
{
    const SkVector radii[4] {
        SkVector::Make(r.fRadTL, r.fRadTL), SkVector::Make(r.fRadTR, r.fRadTR),
        SkVector::Make(r.fRadBR, r.fRadBR), SkVector::Make(r.fRadBL, r.fRadBL) };
    SkRRect rrect;
    rrect.setRectRadii(SkRect::Make(static_cast<const SkIRect&>(r)), radii);
    return rrect;
}

void SurfaceBlurManager::updateClip() noexcept
{
    auto &surface { *static_cast<Surface*>(this->surface()) };
    const Int32 radius { surface.view.bottomRadius() };
    const bool clientClip { props().maskType == MaskType::RoundRect ||
                            (props().maskType == MaskType::SVGPath && !props().svgPathMask.isEmpty()) };

    // No rounded corners: the client's clip as is
    if (radius <= 0 || surface.size().isEmpty())
    {
        if (props().maskType == MaskType::RoundRect)
            fx.setRoundRectClip(props().roundRectMask);
        else if (clientClip)
            fx.setPathClip(props().svgPathMask);
        else
            fx.clearClip();
        return;
    }

    // The surface with its bottom corners rounded, like the view
    const CZRRect corners { SkIRect::MakeSize(surface.size()), 0, 0, radius, radius };

    if (!clientClip)
    {
        fx.setRoundRectClip(corners);
        return;
    }

    // Both: their intersection
    SkPath client;

    if (props().maskType == MaskType::RoundRect)
        client.addRRect(ToSkRRect(props().roundRectMask));
    else
        client = props().svgPathMask;

    SkPath ours;
    ours.addRRect(ToSkRRect(corners));

    SkPath clip;

    if (Op(client, ours, kIntersect_SkPathOp, &clip))
        fx.setPathClip(clip);
    else
        fx.setPathClip(client);
}
