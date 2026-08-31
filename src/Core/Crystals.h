#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <Core/Types.h>
#include <Louvre/Louvre.h>
#include <Louvre/LCompositor.h>
#include <AK/AKApp.h>
#include <Heaven/Compositor/HNCompositor.h>

using namespace CZ;
using namespace CZ::HNCompositorAPI;

class Crystals final : public LCompositor
{
public:
    Crystals() noexcept;
    const std::vector<Client*> &clients() const noexcept { return (std::vector<Client*> &)LCompositor::clients(); };
    std::shared_ptr<AKApp> app;
    std::shared_ptr<Scene> scene;
    std::shared_ptr<HNCompositor> heaven;

private:
    void initialized() noexcept override;
    void uninitialized() noexcept override;
    LFactoryObject *createObjectRequest(LFactoryObject::Type objectType, const void *params) override;
};

#endif // COMPOSITOR_H
