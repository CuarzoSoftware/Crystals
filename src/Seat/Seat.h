#ifndef SEAT_H
#define SEAT_H

#include <Core/Types.h>
#include <Louvre/Seat/LSeat.h>

struct libinput_device;

class Seat final : public LSeat
{
public:
    Seat(const void *params) noexcept;
    void configureLibinputDevice(libinput_device *dev) noexcept;
    void configureInputDevices() noexcept;
    void enabledChanged() override;

    void activeToplevelChanged(LToplevelRole *prev) noexcept override;
    void inputDevicePlugged(std::shared_ptr<CZInputDevice> dev) override;
    void inputEvent(const CZInputEvent &e) noexcept override;
    CZLogger log;
};

#endif // SEAT_H
