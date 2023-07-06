#include "View.h"

View::View(View *parent)
{
    setParent(parent);
}

View::~View()
{
    if (m_parent)
        m_parent->m_children.erase(m_parentLink);

    while (!m_children.empty())
        m_children.back()->setParent(nullptr);
}

VIEW_RENDER_MODE View::renderMode() const
{
    return VIEW_RENDER_MODE_CHILDREN_ONLY;
}

VIEW_TYPE View::type() const
{
    return VIEW_TYPE_CONTAINER;
}

void View::setPosC(const LPoint &pos)
{
    L_UNUSED(pos);
}

const LPoint *View::posC() const
{
    return nullptr;
}

const LPoint *View::sizeC() const
{
    return nullptr;
}

const LTexture *View::texture() const
{
    return nullptr;
}

bool View::mapped() const
{
    return isVisible();
}

Int32 View::scale() const
{
    return 1;
}

UInt32 View::damageId() const
{
    return 0;
}

const LRegion *View::inputRegionC() const
{
    return nullptr;
}

const LRegion *View::opaqueRegionC() const
{
    return nullptr;
}

const LRegion *View::translucentRegionC() const
{
    return nullptr;
}

const LRegion *View::damageC() const
{
    return nullptr;
}

void View::insertAfter(View *prev)
{
    if (!parent() || prev == this)
        return;

    if (!prev)
    {
        m_parent->m_children.erase(m_parentLink);
        m_parent->m_children.push_front(this);
        m_parentLink = m_parent->m_children.begin();
    }
    else
    {
        if (!prev->parent() || prev->parent() != parent())
            return;

        if (prev == m_parent->m_children.back())
        {
            m_parent->m_children.erase(m_parentLink);
            m_parent->m_children.push_back(this);
            m_parentLink = std::prev(m_parent->m_children.end());
        }
        else
        {
            m_parent->m_children.erase(m_parentLink);
            m_parentLink = m_parent->m_children.insert(std::next(prev->m_parentLink), this);
        }
    }
}

void View::paintBegin(Output *output)
{
    L_UNUSED(output);
}

void View::enterOutput(Output *output)
{
    L_UNUSED(output);
}

void View::leaveOutput(Output *output)
{
    L_UNUSED(output);
}

void View::requestNextFrame(bool clearDamage)
{
    L_UNUSED(clearDamage);
}

void View::setVisible(bool visible)
{
    m_visible = visible;
}

bool View::isVisible() const
{
    return m_visible;
}

void View::setParent(View *parent)
{
    if (m_parent)
        m_parent->m_children.erase(m_parentLink);

    m_parent = parent;

    if (parent)
    {
        parent->m_children.push_back(this);
        m_parentLink = std::prev(parent->m_children.end());
    }
}

View *View::parent() const
{
    return m_parent;
}

const std::list<View *> &View::children() const
{
    return m_children;
}
