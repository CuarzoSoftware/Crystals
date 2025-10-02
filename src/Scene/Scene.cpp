#include <Events/CZKeyboardEnterEvent.h>
#include <Events/CZPointerEnterEvent.h>
#include <Scene/Scene.h>
#include <Core/CZCore.h>

Scene::Scene() noexcept
{
    scene = AKScene::Make();
    scene->setRoot(&root);

    for (auto &layer : layers)
        layer.setParent(&root);

    auto core { CZCore::Get() };
    core->sendEvent(CZPointerEnterEvent(), *scene);
    core->sendEvent(CZKeyboardEnterEvent(), *scene);
}
