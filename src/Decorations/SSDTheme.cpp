#include <Decorations/SSDTheme.h>
#include <Ream/RSurface.h>
#include <Ream/RPass.h>
#include <Ream/RImage.h>
#include <CZ/skia/core/SkCanvas.h>
#include <CZ/skia/core/SkRRect.h>
#include <CZ/skia/effects/SkImageFilters.h>
#include <algorithm>

namespace SSDTheme
{

static std::map<std::tuple<Int32, bool, bool>, Shadow> s_shadows;
static std::map<std::tuple<Int32, bool>, std::shared_ptr<RImage>> s_corners;

static std::pair<Int32, Int32> ShadowParams(bool active) noexcept
{
    return active ? std::pair { ShadowActiveRadius, ShadowActiveOffsetY } : std::pair { ShadowInactiveRadius, ShadowInactiveOffsetY };
}

ShadowMargins Margins(bool active) noexcept
{
    const auto [radius, offsetY] { ShadowParams(active) };
    return { radius, std::max(0, radius - offsetY), radius, radius + offsetY };
}

const Shadow &ShadowAsset(Int32 scale, bool active, bool dark) noexcept
{
    scale = std::max(1, scale);
    const auto key { std::make_tuple(scale, active, dark) };

    if (auto it { s_shadows.find(key) }; it != s_shadows.end())
        return it->second;

    Shadow &shadow { s_shadows[key] };
    const auto [radius, offsetY] { ShadowParams(active) };
    const ShadowMargins m { Margins(active) };

    // The smallest frame whose 1 unit middle row/column is past both rounded corners and the blur
    // falloff: everything else is stretched from it.
    const Int32 frame { 2 * BorderRadius + 2 * radius + 1 };
    const SkISize size { m.left + frame + m.right, m.top + frame + m.bottom };
    shadow.center = SkIRect::MakeXYWH(m.left + BorderRadius + radius, m.top + BorderRadius + radius, 1, 1);

    auto surface { RSurface::Make(size, scale, true) };

    if (!surface)
        return shadow;

    {
        auto pass { surface->beginPass(RPassCap_SkCanvas) };
        SkCanvas *c { pass->getCanvas() };
        c->clear(SK_ColorTRANSPARENT);

        const SkRRect rrect { SkRRect::MakeRectXY(SkRect::MakeXYWH(m.left, m.top, frame, frame), BorderRadius, BorderRadius) };

        SkPaint paint;
        paint.setAntiAlias(true);

        // Drop shadow
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setImageFilter(SkImageFilters::DropShadowOnly(0.f, offsetY, radius / 3.f, radius / 3.f, 0x69000000, nullptr));
        c->drawRRect(rrect, paint);

        // Hairline border
        paint.setImageFilter(nullptr);
        paint.setBlendMode(SkBlendMode::kSrcOver);
        paint.setStroke(true);
        paint.setStrokeWidth(1.f);

        if (dark)
        {
            // Dark: a faint light line at the edge (its outer half survives the clear below), a dark
            // one right outside it, so the frame stands out on dark content
            paint.setColor(0x40FFFFFF);
            c->drawRRect(rrect, paint);
            SkRRect outer;
            rrect.outset(1.f, 1.f, &outer);
            paint.setColor(0x99000000);
            c->drawRRect(outer, paint);
        }
        else
        {
            paint.setColor(0x33000000);
            c->drawRRect(rrect, paint);
        }

        // The window covers the center (and the border's inner half)
        paint.setStroke(false);
        paint.setBlendMode(SkBlendMode::kClear);
        c->drawRRect(rrect, paint);
    }

    shadow.image = surface->image();
    return shadow;
}

std::shared_ptr<RImage> CornerMask(Int32 scale, bool left) noexcept
{
    scale = std::max(1, scale);
    const auto key { std::make_tuple(scale, left) };

    if (auto it { s_corners.find(key) }; it != s_corners.end())
        return it->second;

    auto surface { RSurface::Make(SkISize::Make(BorderRadius, BorderRadius), scale, true) };

    if (!surface)
        return {};

    {
        auto pass { surface->beginPass(RPassCap_SkCanvas) };
        SkCanvas *c { pass->getCanvas() };
        c->clear(SK_ColorTRANSPARENT);

        // A quarter disk: the content is kept inside it, the square corner outside is cut.
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setColor(SK_ColorBLACK);
        c->drawCircle(SkPoint::Make(left ? BorderRadius : 0.f, 0.f), BorderRadius, paint);
    }

    auto image { surface->image() };
    s_corners[key] = image;
    return image;
}

void Clear() noexcept
{
    s_shadows.clear();
    s_corners.clear();
}

}
