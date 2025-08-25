#include <Scene/Scene.h>

Scene::Scene() noexcept
{
    scene = AKScene::Make();
    scene->setRoot(&root);

    for (auto &layer : layers)
        layer.setParent(&root);
}
