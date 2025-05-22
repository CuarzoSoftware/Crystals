#include <ui/Scene.h>

Scene::Scene() noexcept
{
    setRoot(&root);

    for (auto &layer : layers)
        layer.setParent(&root);
}
