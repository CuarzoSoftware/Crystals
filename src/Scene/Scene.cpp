#include <Core/Events/CZKeyboardEnterEvent.h>
#include <Core/Events/CZPointerEnterEvent.h>
#include <Core/CZCore.h>
#include <Scene/Scene.h>
#include <Core/Crystals.h>
#include <Louvre/Roles/LSurface.h>

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

    containers.insert(containers.end(), { &animations, &shell, &top });

    for (AKContainer *container : containers)
    {
        container->setParent(&root);
        container->setInputRegion(&empty);
    }

    // Their surfaces (or animations) may be anywhere
    for (AKContainer *container : { &backdrops, &layers[Overlay], &animations, &shell, &top })
        container->enableChildrenClipping(false);

    auto core { CZCore::Get() };
    core->sendEvent(CZPointerEnterEvent(), *scene);
    core->sendEvent(CZKeyboardEnterEvent(), *scene);
}

AKContainer &Scene::containerFor(LSurface &surface) noexcept
{
    if (surface.dndIcon())
        return top;

    if (Crystals::IsDesk(&surface))
        return desktop;

    if (Crystals::IsShell(&surface))
        return shell;

    return layers[surface.layer()];
}
