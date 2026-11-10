#include <Effects/DNDReturnAnimation.h>
#include <Scene/Scene.h>
#include <Louvre/LCompositor.h>
#include <Core/CZEase.h>
#include <Core/CZTimer.h>
#include <list>

static std::list<std::unique_ptr<DNDReturnAnimation>> &Animations() noexcept
{
    static std::list<std::unique_ptr<DNDReturnAnimation>> list;
    return list;
}

void DNDReturnAnimation::Start(std::shared_ptr<RImage> image, const SkRect &from, const SkPoint &to, UInt32 durationMs) noexcept
{
    if (!image || from.isEmpty())
        return;

    Animations().emplace_back(new DNDReturnAnimation(image, from, to, durationMs));
}

void DNDReturnAnimation::Hold(std::shared_ptr<RImage> image, const SkRect &rect, UInt32 durationMs) noexcept
{
    Start(image, rect, SkPoint::Make(rect.x(), rect.y()), durationMs);
}

void DNDReturnAnimation::Clear() noexcept
{
    Animations().clear();
}

DNDReturnAnimation::DNDReturnAnimation(std::shared_ptr<RImage> image, const SkRect &from, const SkPoint &to, UInt32 durationMs) noexcept :
    m_from(from),
    m_to(to),
    m_anim(durationMs,
        [this](CZAnimation *a) { update(a->value()); },
        [this](CZAnimation *) {
            update(1.0);
            m_node.setVisible(false);
            compositor()->repaintAllOutputs();

            // Destroyed outside its own animation callback.
            CZTimer::OneShot(0, [this](CZTimer *){
                Animations().remove_if([this](const auto &a){ return a.get() == this; });
            });
        })
{
    m_node.setImage(image);
    m_node.enableAutoDamage(true);
    m_node.layout().setPositionType(YGPositionTypeAbsolute);
    m_node.layout().setWidth(from.width());
    m_node.layout().setHeight(from.height());

    // Above everything else, like the DnD icon itself.
    m_node.setParent(&GetScene()->root);
    update(0.0);
    m_anim.start();
    compositor()->repaintAllOutputs();
}

DNDReturnAnimation::~DNDReturnAnimation()
{
    m_anim.pause();
}

void DNDReturnAnimation::update(Float64 t) noexcept
{
    const SkScalar e { SkScalar(CZEase::InOutCubic(t)) };
    m_node.layout().setPosition(YGEdgeLeft, m_from.x() + (m_to.x() - m_from.x()) * e);
    m_node.layout().setPosition(YGEdgeTop, m_from.y() + (m_to.y() - m_from.y()) * e);
    compositor()->repaintAllOutputs();
}
