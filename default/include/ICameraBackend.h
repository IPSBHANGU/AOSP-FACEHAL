#pragma once

#include <vector>
#include <functional>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

class ICameraBackend {
public:
    using FrameCallback = std::function<void(const std::vector<uint8_t>& nv21,
                                             int width, int height, int angle)>;

    virtual ~ICameraBackend() = default;

    virtual bool start(FrameCallback cb) = 0;
    virtual void stop() = 0;

    /** VendorCode::* set when last start() returned false; 0 if unknown. */
    virtual int lastStartFailureVendorCode() const { return 0; }
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
