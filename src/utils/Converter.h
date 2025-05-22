#ifndef CONVERTER_H
#define CONVERTER_H

#include <include/core/SkRegion.h>
#include <include/core/SkSurface.h>
#include <seat/Output.h>
#include <AK/AKRRect.h>
#include <LRRect.h>

namespace Converter
{
    sk_sp<SkSurface> SkSurfaceFromOutput(Output *output) noexcept;
    SkPixelGeometry LtSPixelGeometry(LOutput::SubPixel pixGeo) noexcept;
    sk_sp<SkImage> LtSTexture(LTexture *texture, LOutput *output = nullptr);

    inline void LtSRegion(const LRegion &in, SkRegion *out) noexcept
    {
        assert(out);
        Int32 n;
        const LBox *boxes { in.boxes(&n) };
        out->setRects((const SkIRect*)boxes, n);
    }

    inline void StLRegion(const SkRegion &in, LRegion *out) noexcept
    {
        assert(out);
        out->clear();
        SkRegion::Iterator it(in);

        while (!it.done())
        {
            out->addRect(it.rect().x(), it.rect().y(), it.rect().width(), it.rect().height());
            it.next();
        }
    }

    inline void LtARRect(const LRRect &in, AKRRect *out) noexcept
    {
        assert(out);
        out->setXYWH(in.x(), in.y(), in.w(), in.h());
        out->fRadTL = in.fRadTL;
        out->fRadTR = in.fRadTR;
        out->fRadBL = in.fRadBL;
        out->fRadBR = in.fRadBR;
    }
}

#endif // CONVERTER_H
