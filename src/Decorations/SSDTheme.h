#ifndef SSDTHEME_H
#define SSDTHEME_H

#include <Core/Types.h>
#include <CZ/skia/core/SkColor.h>
#include <CZ/Core/CZAdaptive.h>
#include <CZ/skia/core/SkRect.h>
#include <map>
#include <memory>
#include <tuple>

namespace CZ { class RImage; }

/**
 * @brief Server-side decorations style (like Marco's macOS-style toplevels plus a title bar) and
 *        its pre-rendered assets, shared by every decorated toplevel.
 */
namespace SSDTheme
{
    // Geometry (logical units)
    constexpr Int32 TitlebarHeight      { 28 };
    constexpr Int32 BorderRadius        { 10 };   // Window corners (like Marco's CSDBorderRadius)
    constexpr Int32 ResizeOutset        { 6 };    // Invisible resize area around the window
    constexpr Int32 ButtonsInset        { 12 };   // Traffic lights from the left edge
    constexpr Int32 TitleSideInset      { 84 };   // Title kept clear of the buttons (both sides, centered)
    constexpr SkScalar TitleFontSize    { 13.f };

    // Shadows (like Marco's MTheme::CSDShadow*)
    constexpr Int32 ShadowActiveRadius      { 48 };
    constexpr Int32 ShadowActiveOffsetY     { 18 };
    constexpr Int32 ShadowInactiveRadius    { 27 };
    constexpr Int32 ShadowInactiveOffsetY   { 8 };

    // Colors (light, dark), like macOS windows
    inline const CZAdaptiveColor TitlebarActive    { 0xFFEBEBEB, 0xFF383838 };
    inline const CZAdaptiveColor TitlebarInactive  { 0xFFF6F6F6, 0xFF2B2B2B };
    inline const CZAdaptiveColor TitleActive       { 0xD9000000, 0xD9FFFFFF };
    inline const CZAdaptiveColor TitleInactive     { 0x59000000, 0x59FFFFFF };
    inline const CZAdaptiveColor Separator         { 0x1F000000, 0x80000000 };

    struct ShadowMargins { Int32 left, top, right, bottom; };

    /// Room the shadow takes around the window frame (title bar + content).
    ShadowMargins Margins(bool active) noexcept;

    struct Shadow
    {
        std::shared_ptr<RImage> image;
        SkIRect center; ///< Stretchable 9-patch center (logical units)
    };

    /**
     * @brief The window frame's drop shadow and hairline border as a 9-patch: each corner and side
     *        is stretched from this minimal image (cached per scale, activation and color scheme).
     *
     * Dark: a darker border, plus a faint light line inside it (the frame stands out on dark content).
     */
    const Shadow &ShadowAsset(Int32 scale, bool active, bool dark = false) noexcept;

    /**
     * @brief Alpha mask of a bottom corner of the window content (a quarter disk, cached per scale).
     *
     * @param left The bottom-left corner (else the bottom-right one).
     */
    std::shared_ptr<RImage> CornerMask(Int32 scale, bool left) noexcept;

    /// Drops every cached asset (before the graphics context goes away).
    void Clear() noexcept;
}

#endif // SSDTHEME_H
