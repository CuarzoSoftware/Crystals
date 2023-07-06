#ifndef OUTPUT_H
#define OUTPUT_H

#include <LOutput.h>
#include <LRegion.h>

using namespace Louvre;

class Compositor;
class View;

class Output : public LOutput
{
public:
    Output();

    Int32 topbarHeight;

    void fullDamage();
    void calculateNewDamage(View *view, bool force = false);
    void drawOpaqueDamage(View *view, bool force = false);
    void drawTranslucentDamage(View *view, bool force = false);

    void initializeGL() override;
    void resizeGL() override;
    void paintGL() override;

    LRegion damage, newDamage;

    // Output rect since the last paintGL()
    LRect lastRectC;

    // Almacena recta del cursor (si no es posible composición por hardware)
    LRect cursorRectG[2];

    LSurface *fullscreenSurface = nullptr;

    // Vars used in each paintGL call
    Compositor *c;
    LPainter *p;
    Int32 n, w, h;
    LRect rect;
    LBox *boxes;
    LRegion opaqueTransposedCSum;
};

#endif // OUTPUT_H
