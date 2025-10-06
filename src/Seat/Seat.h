#ifndef SEAT_H
#define SEAT_H

#include <Core/Types.h>
#include <LSeat.h>

struct libinput_device;

class Seat final : public LSeat
{
public:
    Seat(const void *params) noexcept;
    void activeToplevelChanged(LToplevelRole *prev) noexcept;
    void configureLibinputDevice(libinput_device *dev) noexcept;
    void configureInputDevices() noexcept;
    void enabledChanged() override;
    void inputDevicePluggedEvent(const CZInputDevicePluggedEvent &e) override;
    void inputEvent(const CZInputEvent &e) noexcept override;
    CZLogger log;
};

#endif // SEAT_H
