#ifndef LVIEW_H
#define LVIEW_H

#include <LObject.h>
#include <LRegion.h>
#include <map>

enum VIEW_RENDER_MODE
{
    VIEW_RENDER_MODE_ITSELF_ONLY,
    VIEW_RENDER_MODE_CHILDREN_ONLY,
    VIEW_RENDER_MODE_CHILDREN_FIRST,
    VIEW_RENDER_MODE_CHILDREN_AFTER
};

enum VIEW_TYPE
{
    VIEW_TYPE_CONTAINER,
    VIEW_TYPE_SURFACE
};

class Output;

class View : public LObject
{
public:
    View(View *parent = nullptr);
    virtual ~View();
    virtual VIEW_RENDER_MODE renderMode() const;
    virtual VIEW_TYPE type() const;
    virtual void setPosC(const LPoint &pos);
    virtual const LPoint *posC() const;
    virtual const LPoint *sizeC() const;
    virtual const LTexture *texture() const;
    virtual bool mapped() const;
    virtual Int32 scale() const;
    virtual UInt32 damageId() const;

    virtual const LRegion *inputRegionC() const;
    virtual const LRegion *opaqueRegionC() const;
    virtual const LRegion *translucentRegionC() const;
    virtual const LRegion *damageC() const;


    // Events
    void insertAfter(View *prev);
    virtual void paintBegin(Output *output);
    virtual void enterOutput(Output *output);
    virtual void leaveOutput(Output *output);
    virtual void requestNextFrame(bool clearDamage = true);

    void setVisible(bool visible);
    bool isVisible() const;

    void setParent(View *parent);
    View *parent() const;
    const std::list<View*>&children() const;

    /****** Data used by outputs ******/

    // If completly occluded by other views
    bool occluded = false;

    // Cache of mapped() call
    bool isRenderable = false;

    // Cache scale() == Global Scale
    bool bufferScaleMatchGlobalScale = false;

    // Cache rect (posC(), sizeC())
    LRect currentRectC;

    // Cached data for each output
    struct OutputData
    {
        UInt32 lastRenderedDamageId;
        LRect previousRectC;
        bool changedOrder = true;
    };
    map<LOutput*,OutputData>outputsMap;

    // Handle to prevent looking in the map each time
    OutputData *currentOutputData;

    // paintGL tmp regions
    LRegion currentOpaqueTransposedC;
    LRegion currentDamageTransposedC;
    LRegion currentOpaqueTransposedCSum;
    LRegion currentTraslucentTransposedC;

    // Imp
    bool m_visible = true;
    View *m_parent = nullptr;
    std::list<View*>::iterator m_parentLink;
    std::list<View*>m_children;
};

#endif // LVIEW_H
