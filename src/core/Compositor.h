#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <LCompositor.h>
#include <core/Global.h>

class Compositor final : public LCompositor
{
public:
    Compositor() noexcept;
    ~Compositor();
    std::unique_ptr<Application> app;

private:
    void initialized() override;
    void uninitialized() override;
    bool createGlobalsRequest() override;
    LFactoryObject *createObjectRequest(LFactoryObject::Type objectType, const void *params) override;
};

#endif // COMPOSITOR_H
