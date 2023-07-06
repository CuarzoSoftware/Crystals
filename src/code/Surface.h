#ifndef SURFACE_H
#define SURFACE_H

#include <LSurface.h>
#include <map>

using namespace Louvre;
using namespace std;
class SurfaceView;

class Surface : public LSurface
{
public:
    Surface(LSurface::Params *params, GLuint textureUnit = 1);
    ~Surface();
    void mappingChanged() override;
    void orderChanged() override;
    void minimizedChanged() override;
    SurfaceView *mainView;

    // Temp variables used in paintGL()
    bool firstMap = true;
};

#endif // SURFACE_H
