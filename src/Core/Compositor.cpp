#include <Protocols/BackgroundBlur/GBackgroundBlurManager.h>
#include <Protocols/InvisibleRegion/GInvisibleRegionManager.h>
#include <Protocols/SvgPath/GSvgPathManager.h>

#include <LGlobal.h>

#include <Core/CZCursorShape.h>
#include <Core/Compositor.h>
#include <Core/Log.h>
#include <Scene/Scene.h>

#include <Roles/Surface.h>
#include <Roles/SubsurfaceRole.h>

#include <Seat/Seat.h>
#include <Seat/Output.h>
#include <Seat/Pointer.h>
#include <Seat/Keyboard.h>

#include <Effects/SurfaceBlurManager.h>

#include <Cursor/LCursorSource.h>
#include <LCursor.h>

Compositor::Compositor() noexcept
{
    LBackgroundBlur::MaskCaps.set(LBackgroundBlur::RoundRectMaskCap | LBackgroundBlur::SVGPathMaskCap);
}

void Compositor::initialized() noexcept
{
    app = AKApp::GetOrMake();
    scene = Scene::Make();
    GetSeat()->configureInputDevices();
    initCursor();
    LCompositor::initialized();
    Log(CZInfo, CZLN, "Compositor initialized");
}

void Compositor::uninitialized() noexcept {}

bool Compositor::createGlobalsRequest()
{
    using namespace CZ::Protocols;
    createGlobal<BackgroundBlur::GBackgroundBlurManager>();
    createGlobal<InvisibleRegion::GInvisibleRegionManager>();
    createGlobal<SvgPath::GSvgPathManager>();
    return LCompositor::createGlobalsRequest();
}

void Compositor::initCursor() noexcept
{
    const std::vector<std::vector<const char*>> names
    {
      /* Default */        { "left_ptr", "arrow" },
      /* ContextMenu */    { "context-menu", "arrow" },
      /* Help */           { "question_arrow", "help" },
      /* Pointer */        { "hand2", "pointer" },
      /* Progress */       { "watch", "left_ptr_watch" },
      /* Wait */           { "watch", "left_ptr_watch" },
      /* Cell */           { "xterm", "crosshair" },
      /* Crosshair */      { "crosshair", "tcross" },
      /* Text */           { "xterm", "ibeam" },
      /* VerticalText */   { "xterm", "ibeam" },
      /* Alias */          { "alias", "draped_box" },
      /* Copy */           { "copy", "draped_box" },
      /* Move */           { "move", "fleur" },
      /* NoDrop */         { "no-drop", "circle" },
      /* NotAllowed */     { "not-allowed", "circle" },
      /* Grab */           { "grab", "fleur" },
      /* Grabbing */       { "grabbing", "fleur" },
      /* ResizeR */        { "right_side", "e-resize" },
      /* ResizeT */        { "top_side", "n-resize" },
      /* ResizeTR */       { "top_right_corner", "ne-resize" },
      /* ResizeTL */       { "top_left_corner", "nw-resize" },
      /* ResizeB */        { "bottom_side", "s-resize" },
      /* ResizeBR */       { "bottom_right_corner", "se-resize" },
      /* ResizeBL */       { "bottom_left_corner", "sw-resize" },
      /* ResizeL */        { "left_side", "w-resize" },
      /* ResizeLR */       { "sb_h_double_arrow", "h_double_arrow" },
      /* ResizeTB */       { "sb_v_double_arrow", "v_double_arrow" },
      /* ResizeTRBL */     { "top_right_corner", "bottom_left_corner" },
      /* ResizeTLBR */     { "top_left_corner", "bottom_right_corner" },
      /* ResizeColumn */   { "sb_h_double_arrow", "h_double_arrow" },
      /* ResizeRow */      { "sb_v_double_arrow", "v_double_arrow" },
      /* AllScroll */      { "fleur", "cross" },
      /* ZoomIn */         { "zoom-in", "plus" },
      /* ZoomOut */        { "zoom-out", "minus" },
      /* DragAndDropAsk */ { "question_arrow", "hand2" },
      /* MoveOrResize */   { "fleur", "hand2" },
    };

    const char *theme {};
    const Int32 size { 64 };

    for (size_t i = 0; i < names.size(); i++)
    {
        const auto shape { CZCursorShape(i+1) };
        const auto &set { names[i] };

        for (const char *name : set)
        {
            cursor()->setShapeAsset(shape, LCursorSource::MakeFromTheme(name, theme, size));

            if (cursor()->getShapeAsset(shape))
                break;
        }

        if (!cursor()->getShapeAsset(shape))
            Log(CZWarning, CZLN, "Could not find a cursor for shape {}", i+1);
    }
}

LFactoryObject *Compositor::createObjectRequest(LFactoryObject::Type objectType, const void *params)
{
    switch (objectType)
    {
    case LFactoryObject::Type::LSurface:
        return new Surface(params);
    case LFactoryObject::Type::LSubsurfaceRole:
        return new SubsurfaceRole(params);
    case LFactoryObject::Type::LOutput:
        return new Output(params);
    case LFactoryObject::Type::LSeat:
        return new Seat(params);
    case LFactoryObject::Type::LPointer:
        return new Pointer(params);
    case LFactoryObject::Type::LKeyboard:
        return new Keyboard(params);
    case LFactoryObject::Type::LBackgroundBlur:
        return new SurfaceBlurManager(params);
    default:
        return nullptr;
    }
}
