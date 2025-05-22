#include <include/gpu/ganesh/gl/GrGLBackendSurface.h>
#include <include/gpu/ganesh/SkSurfaceGanesh.h>
#include <include/gpu/ganesh/SkImageGanesh.h>
#include <include/gpu/ganesh/GrDirectContext.h>
#include <include/gpu/ganesh/SkImageGanesh.h>
#include <include/gpu/ganesh/GrBackendSurface.h>
#include <include/core/SkColorSpace.h>
#include <utils/Converter.h>
#include <seat/Output.h>
#include <core/Application.h>
#include <AK/AKGLContext.h>
#include <AK/AKLog.h>
#include <LTexture.h>

static sk_sp<SkColorSpace> colorSpace = SkColorSpace::MakeSRGB();

sk_sp<SkSurface> Converter::SkSurfaceFromOutput(Output *output) noexcept
{
    const GrGLFramebufferInfo fbInfo
    {
        .fFBOID = output->framebuffer()->id(),
        .fFormat = GL_RGB8_OES
    };

    const GrBackendRenderTarget backendTarget = GrBackendRenderTargets::MakeGL(
        output->realBufferSize().w(),
        output->realBufferSize().h(),
        0, 0,
        fbInfo);

    const SkSurfaceProps skSurfaceProps(0, LtSPixelGeometry(output->subPixel()));

    auto surface = SkSurfaces::WrapBackendRenderTarget(
        G::app()->glContext()->skContext().get(),
        backendTarget,
        fbInfo.fFBOID == 0 ? GrSurfaceOrigin::kBottomLeft_GrSurfaceOrigin : GrSurfaceOrigin::kTopLeft_GrSurfaceOrigin,
        SkColorType::kRGB_888x_SkColorType,
        colorSpace,
        &skSurfaceProps);

    assert("Failed to create SkSurface wrapper for Output framebuffer" && surface);
    return surface;
}

SkPixelGeometry Converter::LtSPixelGeometry(LOutput::SubPixel pixGeo) noexcept
{
    switch (pixGeo)
    {
    case LOutput::SubPixel::HorizontalRGB:
        return SkPixelGeometry::kRGB_H_SkPixelGeometry;
    case LOutput::SubPixel::HorizontalBGR:
        return SkPixelGeometry::kBGR_H_SkPixelGeometry;
    case LOutput::SubPixel::VerticalRGB:
        return SkPixelGeometry::kRGB_V_SkPixelGeometry;
    case LOutput::SubPixel::VerticalBGR:
        return SkPixelGeometry::kBGR_V_SkPixelGeometry;
    default:
        return SkPixelGeometry::kUnknown_SkPixelGeometry;
    }
}

sk_sp<SkImage> Converter::LtSTexture(LTexture *texture, LOutput *output)
{
    if (!texture)
        return nullptr;

    // TODO: Handle all formats

    GrGLTextureInfo skTextureInfo;
    GrBackendTexture skTexture;
    skTextureInfo.fFormat = GL_BGRA8_EXT;
    skTextureInfo.fID = texture->id(output);
    skTextureInfo.fTarget = texture->target();

    skTexture = GrBackendTextures::MakeGL(
        texture->sizeB().w(),
        texture->sizeB().h(),
        skgpu::Mipmapped::kNo,
        skTextureInfo);

    auto image = SkImages::BorrowTextureFrom(
        akApp()->glContext()->skContext().get(),
        skTexture,
        GrSurfaceOrigin::kTopLeft_GrSurfaceOrigin,
        kBGRA_8888_SkColorType,
        texture->premultipliedAlpha() ? SkAlphaType::kPremul_SkAlphaType : SkAlphaType::kUnpremul_SkAlphaType,
        colorSpace);

    if (!image)
        AKLog::error("[Converter::SkImageFromLTexture] Failed to create SkImage wrapper for LTexture.");

    return image;
}
