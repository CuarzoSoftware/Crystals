#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <Core/Types.h>
#include <LCompositor.h>
#include <AKApp.h>

using namespace CZ;

class Compositor final : public LCompositor
{
public:
    Compositor() noexcept;
    std::shared_ptr<AKApp> app;
    std::shared_ptr<Scene> scene;

private:
    void initialized() noexcept override;
    void uninitialized() noexcept override;
    bool createGlobalsRequest() override;
    void initCursor() noexcept;
    LFactoryObject *createObjectRequest(LFactoryObject::Type objectType, const void *params) override;
};

#endif // COMPOSITOR_H
