#pragma once

#include <utils/Log.h>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

#include "ICameraBackend.h"
#include <memory>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

class CameraClient {
public:
    using FrameCallback = ICameraBackend::FrameCallback;

    CameraClient();
    ~CameraClient();

    bool start(FrameCallback cb);
    void stop();

    /** After failed start(): VendorCode::CAMERA_* from FaceEngine.h */
    int lastStartFailureVendorCode() const;

    static void warmUpAtHalStart();

private:
    std::shared_ptr<ICameraBackend> mBackend;
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
