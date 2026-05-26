#pragma once

#include "ICameraBackend.h"

#include <camera/NdkCameraManager.h>
#include <camera/NdkCameraDevice.h>
#include <camera/NdkCameraCaptureSession.h>
#include <camera/NdkCaptureRequest.h>
#include <camera/NdkCameraMetadata.h>
#include <media/NdkImageReader.h>

#include <atomic>
#include <mutex>
#include <string>
#include <condition_variable>
#include <thread>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

// Camera client implemented on top of libcamera2ndk_vendor. Talks directly to
// the camera HAL (no cameraserver) which is the only camera path allowed for
// vendor processes. Replaces the hand-rolled AIDL/HIDL backends.
class Camera2NdkBackend : public ICameraBackend {
public:
    Camera2NdkBackend();
    ~Camera2NdkBackend() override;

    bool start(FrameCallback cb) override;
    void stop() override;

    int lastStartFailureVendorCode() const override;

    /** Probe cameras once at HAL process start; caches id so enroll/auth opens immediately. */
    static void warmUpAtHalStart();

private:
    bool waitForNonEmptyCameraList(int timeoutMs);
    bool selectFrontCamera(std::string& outId);
    void teardown(std::unique_lock<std::mutex>& lock);
    void noteFailure(int vendorCode);  // VendorCode::CAMERA_* from FaceEngine.h

    static void onImageAvailable(void* ctx, AImageReader* reader);

    static void onDeviceDisconnected(void* ctx, ACameraDevice* dev);
    static void onDeviceError(void* ctx, ACameraDevice* dev, int err);

    static void onSessionActive(void* ctx, ACameraCaptureSession* s);
    static void onSessionReady(void* ctx, ACameraCaptureSession* s);
    static void onSessionClosed(void* ctx, ACameraCaptureSession* s);

    std::mutex mLock;
    FrameCallback mCallback;
    std::atomic<bool> mIsRunning{false};

    ACameraManager*                 mMgr   = nullptr;
    AImageReader*                   mRdr   = nullptr;
    ANativeWindow*                  mWin   = nullptr;  // owned by reader
    ACameraDevice*                  mDev   = nullptr;
    ACaptureSessionOutput*          mOut   = nullptr;
    ACaptureSessionOutputContainer* mOutC  = nullptr;
    ACameraCaptureSession*          mSess  = nullptr;
    ACaptureRequest*                mReq   = nullptr;
    ACameraOutputTarget*            mTgt   = nullptr;

    ACameraDevice_StateCallbacks    mDevCb{};
    ACameraCaptureSession_stateCallbacks mSessCb{};
    AImageReader_ImageListener      mImgCb{};

    std::string mCameraId;
    int mWidth  = 640;
    int mHeight = 480;
    int mFrameAngle = 0;
    int mLastStartFailure = 0;

    std::condition_variable mCallbackCv;
    bool mInCallback = false;
    bool mIsStopping = false;
    std::thread::id mCallbackThreadId;
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
