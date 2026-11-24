#include <Core/Events/CZKeyboardEnterEvent.h>
#include <Core/Events/CZPointerEnterEvent.h>
#include <Core/CZCore.h>
#include <Scene/Scene.h>
#include <Effects/TopbarAutoHide.h>
#include <Core/Crystals.h>
#include <Louvre/Roles/LSurface.h>
#include <Louvre/LCompositor.h>
#include <Louvre/Seat/LOutput.h>
#include <Core/CZEase.h>

// The desktop fading in from black once unlocked
static constexpr UInt32 UnlockFadeMs { 350 };

Scene::Scene() noexcept
{
    scene = AKScene::Make();
    scene->setRoot(&root);

    // Pure structure: no input of their own (an unset region would take it all, hiding everything
    // below from AKScene::nodeAt()).
    const SkRegion empty;
    root.setInputRegion(&empty);

    std::vector<AKContainer*> containers { &desktop, &backdrops };

    for (auto &layer : layers)
        containers.push_back(&layer);

    containers.insert(containers.end(), { &animations, &shell, &expose, &docks, &notifications, &popups, &top, &blackouts, &lock });

    for (AKContainer *container : containers)
    {
        container->setParent(&root);
        container->setInputRegion(&empty);
    }

    // Their surfaces (or animations) may be anywhere
    for (AKContainer *container : { &backdrops, &layers[Overlay], &animations, &shell, &expose, &docks, &notifications, &popups, &top, &blackouts, &lock })
        container->enableChildrenClipping(false);

    // Above everything, hidden until unlocked
    unlockFade.setParent(&root);
    unlockFade.setInputRegion(&empty);
    unlockFade.layout().setPositionType(YGPositionTypeAbsolute);
    unlockFade.setVisible(false);

    m_unlockFadeAnim.setDuration(UnlockFadeMs);
    m_unlockFadeAnim.onUpdate.subscribe(this, [this](CZAnimation *a){
        unlockFade.setOpacity(1.f - SkScalar(CZEase::OutCubic(a->value())));
    });
    m_unlockFadeAnim.onFinish.subscribe(this, [this](CZAnimation*){
        unlockFade.setVisible(false);
    });

    auto core { CZCore::Get() };
    core->sendEvent(CZPointerEnterEvent(), *scene);
    core->sendEvent(CZKeyboardEnterEvent(), *scene);
}

void Scene::setLocked(bool locked) noexcept
{
    if (m_locked == locked)
        return;

    m_locked = locked;

    // Only the lock surfaces (Output's target clears to black). Back as they were once unlocked
    if (locked)
    {
        m_hiddenByLock.clear();

        for (AKNode *node : root.children(true))
        {
            if (node == &lock || node == &unlockFade || !node->visible())
                continue;

            m_hiddenByLock.emplace_back(node);
            node->setVisible(false);
        }
    }
    else
    {
        for (auto &node : m_hiddenByLock)
            if (node)
                node->setVisible(true);

        m_hiddenByLock.clear();
    }

    m_unlockFadeAnim.pause();

    if (locked)
    {
        unlockFade.setVisible(false);
        return;
    }

    // Unlocked: the desktop fades in from black (the lock screen faded out to black, or vanished)
    SkIRect bounds { SkIRect::MakeEmpty() };

    for (LOutput *output : compositor()->outputs())
        bounds.join(output->rect());

    unlockFade.layout().setPosition(YGEdgeLeft, bounds.x());
    unlockFade.layout().setPosition(YGEdgeTop, bounds.y());
    unlockFade.layout().setWidth(bounds.width());
    unlockFade.layout().setHeight(bounds.height());
    unlockFade.setOpacity(1.f);
    unlockFade.setVisible(true);
    m_unlockFadeAnim.reset();
    m_unlockFadeAnim.start();
}

AKContainer &Scene::containerFor(LSurface &surface) noexcept
{
    if (surface.sessionLock())
        return lock;

    if (surface.dndIcon())
        return top;

    if (Crystals::IsDesk(&surface))
        return desktop;

    if (Crystals::IsNotifications(&surface))
        return notifications;

    // The docks (Shelf) stay above Exposé, top bars go under its workspace bar
    if (Crystals::IsShell(&surface))
        return surface.layerRole() && TopbarAutoHide::IsTopbar(*surface.layerRole()) ? shell : docks;

    return layers[surface.layer()];
}
