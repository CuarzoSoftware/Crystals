#include <roles/Surface.h>
#include <ui/Scene.h>

void Surface::damageChanged()           { view.syncDamage(); needsTextureUpdate = true; }
void Surface::opaqueRegionChanged()     { view.syncOpaqueRegion(); }
void Surface::invisibleRegionChanged()  { view.syncInvisibleRegion(); }
void Surface::inputRegionChanged()      { view.syncInputRegion(); }

void Surface::bufferSizeChanged()       { needsTextureUpdate = true; }
void Surface::sizeChanged()             { view.syncSize(); }
void Surface::srcRectChanged()          { view.syncSrcRect(); }
void Surface::bufferScaleChanged()      { view.syncScale(); }
void Surface::bufferTransformChanged()  { view.syncTransform(); }

void Surface::roleChanged()
{

}

void Surface::parentChanged()
{

}

void Surface::mappingChanged()
{
    LSurface::mappingChanged();
}

void Surface::orderChanged()
{
    if (prevSurface() && prevSurface()->layer() == layer())
        view.insertAfter(&static_cast<Surface*>(prevSurface())->view);
    else if (nextSurface() && nextSurface()->layer() == layer())
        view.insertBefore(&static_cast<Surface*>(nextSurface())->view);
}

void Surface::layerChanged()
{
    view.setParent(&G::scene()->layers[layer()]);
}
