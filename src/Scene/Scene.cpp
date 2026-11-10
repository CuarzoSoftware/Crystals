#include <Core/Events/CZKeyboardEnterEvent.h>
#include <Core/Events/CZPointerEnterEvent.h>
#include <Core/CZCore.h>
#include <Scene/Scene.h>

Scene::Scene() noexcept
{
    scene = AKScene::Make();
    scene->setRoot(&root);

    // Pure structure: no input of their own (an unset region would take it all, hiding everything
    // below from AKScene::nodeAt()).
    const SkRegion empty;
    root.setInputRegion(&empty);

    for (auto &layer : layers)
    {
        layer.setParent(&root);
        layer.setInputRegion(&empty);
    }

    auto core { CZCore::Get() };
    core->sendEvent(CZPointerEnterEvent(), *scene);
    core->sendEvent(CZKeyboardEnterEvent(), *scene);
}
