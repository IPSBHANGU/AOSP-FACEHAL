/*
 * Copyright (C) 2026 The Project MiLahaina
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "MiLahainaFaceHal"
#include <log/log.h>
#include "Session.h"
#include "CancellationSignal.h"
#include "FaceEngine.h"
#include "FaceStorageCallbacks.h"
#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>
#include <aidl/android/hardware/biometrics/face/AuthenticationFrame.h>
#include <aidl/android/hardware/biometrics/face/EnrollmentFrame.h>
#include <android-base/logging.h>
#include <aidl/android/hardware/biometrics/common/DisplayState.h>
#include <unistd.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::AcquiredInfo;
using aidl::android::hardware::biometrics::face::AuthenticationFrame;
using aidl::android::hardware::biometrics::face::EnrollmentFrame;
using aidl::android::hardware::biometrics::face::Error;

struct Session::SessionCallbackQueue {
    std::mutex lock;
    std::condition_variable cv;
    std::queue<std::function<void()>> queue;
    bool running = true;
};

Session::Session(int32_t userId, const std::shared_ptr<ISessionCallback> &cb)
    : mUserId(userId), mCb(cb), mEngine(FaceEngine::getInstance()),
      mCryptoClient(CryptoClient::getInstance()), mCameraClient(std::make_shared<CameraClient>()),
      mIsAuthenticating(false), mIsEnrolling(false), mIsDetectingInteraction(false), mEnrollRemaining(0), mCurrentChallenge(0) {
  if (!mEngine.init(getFaceEngineCallbacks())) {
    LOG(ERROR) << "Failed to initialize FaceEngine in Session";
  }
  mEngine.restoreEnrollments(mUserId);

  mEngine.setSessionFrameCallback([this](const std::vector<uint8_t> &frame,
                                         int width, int height,
                                         int angle) -> int {
    return this->onCameraFrame(frame, width, height, angle);
  });

  mCallbackQueueState = std::make_shared<SessionCallbackQueue>();
  mCallbackWorker = std::thread([state = mCallbackQueueState]() {
      while (true) {
          std::function<void()> task;
          {
              std::unique_lock<std::mutex> lock(state->lock);
              state->cv.wait(lock, [state]() { return !state->running || !state->queue.empty(); });
              if (!state->running && state->queue.empty()) {
                  break;
              }
              task = std::move(state->queue.front());
              state->queue.pop();
          }
          if (task) {
              task();
          }
      }
  });
}

Session::~Session() {
  mEngine.setSessionFrameCallback(nullptr);
  cancel();

  if (mCallbackQueueState) {
      std::lock_guard<std::mutex> lock(mCallbackQueueState->lock);
      mCallbackQueueState->running = false;
      mCallbackQueueState->cv.notify_all();
  }
  if (mCallbackWorker.joinable()) {
      mCallbackWorker.detach();
  }
}

void Session::cancel() {
  if (mIsAuthenticating || mIsEnrolling || mIsDetectingInteraction) {
    mCb->onError(Error::CANCELED, 0);
  }
  mIsAuthenticating = false;
  mIsEnrolling = false;
  mIsDetectingInteraction = false;
  mCameraClient->stop();
}

int Session::onCameraFrame(const std::vector<uint8_t> &frame, int width,
                           int height, int angle) {
  LOG(INFO) << "onCameraFrame: received frame of size " << frame.size() << " ("
            << width << "x" << height << ") angle=" << angle
            << " mIsEnrolling=" << mIsEnrolling
            << ", mIsAuthenticating=" << mIsAuthenticating
            << ", mIsDetectingInteraction=" << mIsDetectingInteraction;
  if (mIsAuthenticating) {
    float score = 0.0f;
    int32_t matchedFaceId = -1;
    int res = mEngine.authenticate(frame, width, height, mUserId, score,
                                   matchedFaceId);
    LOG(INFO) << "onCameraFrame: authenticate res=" << res << " score=" << score
              << " faceId=" << matchedFaceId;
    if (res == 0) {
      mIsAuthenticating = false;
      mCameraClient->stop();
      HardwareAuthToken hat;
      postCallback([cb = mCb, matchedFaceId, hat]() {
          cb->onAuthenticationSucceeded(matchedFaceId, hat);
      });
    } else if (res > 0) {
      // No match
      if (res != VendorCode::KEEP) {
        postCallback([cb = mCb]() {
            cb->onAuthenticationFailed();
        });
      }
      AuthenticationFrame authFrame;
      authFrame.data.acquiredInfo = AcquiredInfo::VENDOR;
      authFrame.data.vendorCode = res;
      postCallback([cb = mCb, authFrame]() {
          cb->onAuthenticationFrame(authFrame);
      });
    }
    return res;
  } else if (mIsDetectingInteraction) {
    int res = mEngine.analyzeFaceQuality(frame, width, height);
    LOG(INFO) << "onCameraFrame: detectInteraction res=" << res;
    if (res == VendorCode::FACE_OK) {
      mIsDetectingInteraction = false;
      mCameraClient->stop();
      postCallback([cb = mCb]() {
          cb->onInteractionDetected();
      });
    }
    return res;
  } else if (mIsEnrolling) {
    int32_t outFaceId = 0;
    int res = mEngine.enroll(mUserId, frame, width, height, outFaceId);
    int progress = mEngine.getEnrollmentProgress();
    int totalFrames = FaceEngine::ENROLL_REQUIRED_GOOD_FRAMES;
    int remaining = totalFrames - (mEngine.mEnrollFrameCount);

    LOG(INFO) << "onCameraFrame: enroll result=" << res
              << " progress=" << progress << "%";

    if (res == VendorCode::FACE_OK) {
      // Enrollment finished successfully
      mIsEnrolling = false;
      mCameraClient->stop();
      postCallback([cb = mCb, outFaceId]() {
          cb->onEnrollmentProgress(outFaceId, 0);
      });
    } else if (res == VendorCode::KEEP) {
      // Good frame, need more. Report progress.
      postCallback([cb = mCb, outFaceId, remaining]() {
          cb->onEnrollmentProgress(outFaceId, remaining);
      });
    } else {
      // Bad quality or error — report vendor code for UI feedback
      EnrollmentFrame enrollFrame;
      enrollFrame.data.acquiredInfo = AcquiredInfo::VENDOR;
      enrollFrame.data.vendorCode = res;
      postCallback([cb = mCb, enrollFrame]() {
          cb->onEnrollmentFrame(enrollFrame);
      });
    }
    return res;
  }
  return -1;
}

ScopedAStatus Session::generateChallenge(void) {
  mCurrentChallenge = mCryptoClient->generateChallenge();

  if (mCb != nullptr) {
    mCb->onChallengeGenerated(mCurrentChallenge);
  }

  return ScopedAStatus::ok();
}

ScopedAStatus Session::revokeChallenge(int64_t challenge) {
  mCb->onChallengeRevoked(challenge);
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::getEnrollmentConfig(EnrollmentType enrollmentType,
                             std::vector<EnrollmentStageConfig> *_aidl_return) {
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enroll(const HardwareAuthToken & /*hat*/, EnrollmentType type,
                const std::vector<Feature> &features,
                const std::optional<NativeHandle> &previewSurface,
                std::shared_ptr<ICancellationSignal> *_aidl_return) {
  LOG(INFO) << "Session::enroll user=" << mUserId
            << " type=" << static_cast<int>(type)
            << " features=" << features.size();
  mIsEnrolling = true;

  // Start direct Camera Provider client for native enrollment.
  // Must route through FaceEngine::onCameraFrame so IVisionService (App preview)
  // and Session enroll both receive every frame.
  bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
      mEngine.onCameraFrame(frame, width, height, angle);
  });
  LOG(INFO) << "Session::enroll CameraClient->start returned " << started;
  if (!started) {
    int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
    if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
    mIsEnrolling = false;
    mCb->onError(Error::VENDOR, v);
    LOG(ERROR) << "Session::enroll camera failed, onError VENDOR vendorCode=" << v;
  }

  *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enrollWithOptions(const FaceEnrollOptions &options,
                           std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return enroll(options.hardwareAuthToken, options.enrollmentType,
                options.features, std::nullopt, _aidl_return);
}

ScopedAStatus Session::authenticate(int64_t operationId,
                                    std::shared_ptr<ICancellationSignal>* _aidl_return) {
    LOG(INFO) << "Session::authenticate user=" << mUserId
              << " operationId=" << operationId;
    mIsAuthenticating = true;

    bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
        mEngine.onCameraFrame(frame, width, height, angle);
    });
    LOG(INFO) << "Session::authenticate CameraClient->start returned " << started;
    if (!started) {
      int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
      if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
      mIsAuthenticating = false;
      mCb->onError(Error::VENDOR, v);
      LOG(ERROR) << "Session::authenticate camera failed, onError VENDOR vendorCode=" << v;
    }

    *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
    return ScopedAStatus::ok();
}

ScopedAStatus
Session::detectInteraction(std::shared_ptr<ICancellationSignal> *_aidl_return) {
    LOG(INFO) << "Session::detectInteraction";
    mIsDetectingInteraction = true;

    bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
        mEngine.onCameraFrame(frame, width, height, angle);
    });
    LOG(INFO) << "Session::detectInteraction CameraClient->start returned " << started;
    if (!started) {
      int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
      if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
      mIsDetectingInteraction = false;
      mCb->onError(Error::VENDOR, v);
      LOG(ERROR) << "Session::detectInteraction camera failed, onError VENDOR vendorCode=" << v;
    }

    *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
    return ScopedAStatus::ok();
}

ScopedAStatus Session::enumerateEnrollments(void) {
  std::vector<int32_t> enrollments = mEngine.getEnrolledFaceIds(mUserId);
  mCb->onEnrollmentsEnumerated(enrollments);
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::removeEnrollments(const std::vector<int32_t> &enrollmentIds) {
  for (int32_t id : enrollmentIds) {
    mEngine.deleteEnrollment(mUserId, id);
  }
  mCb->onEnrollmentsRemoved(enrollmentIds);
  return ScopedAStatus::ok();
}

ScopedAStatus Session::getFeatures() {
  std::vector<Feature> features;
  if (mEngine.getRequireAttention()) {
      features.push_back(Feature::REQUIRE_ATTENTION);
  }
  if (mEngine.getRequireDiversePoses()) {
      features.push_back(Feature::REQUIRE_DIVERSE_POSES);
  }
  mCb->onFeaturesRetrieved(features);
  return ScopedAStatus::ok();
}

ScopedAStatus Session::setFeature(const HardwareAuthToken &hat, Feature feature,
                                  bool enabled) {
  if (feature == Feature::REQUIRE_ATTENTION) {
      mEngine.setRequireAttention(enabled);
      LOG(INFO) << "REQUIRE_ATTENTION set to " << (enabled ? "true" : "false");
  } else if (feature == Feature::REQUIRE_DIVERSE_POSES) {
      mEngine.setRequireDiversePoses(enabled);
      LOG(INFO) << "REQUIRE_DIVERSE_POSES set to " << (enabled ? "true" : "false");
  }
  return ScopedAStatus::ok();
}

ScopedAStatus Session::getAuthenticatorId(void) {
  mCb->onAuthenticatorIdRetrieved(0);
  return ScopedAStatus::ok();
}

ScopedAStatus Session::invalidateAuthenticatorId(void) {
  return ScopedAStatus::ok();
}

ScopedAStatus Session::resetLockout(const HardwareAuthToken &hat) {
  mCb->onLockoutCleared();
  return ScopedAStatus::ok();
}

ScopedAStatus Session::close(void) {
  cancel();
  mCb->onSessionClosed();
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enrollWithContext(const HardwareAuthToken &hat, EnrollmentType type,
                           const std::vector<Feature> &features,
                           const std::optional<NativeHandle> &previewSurface,
                           const OperationContext &context,
                           std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return enroll(hat, type, features, previewSurface, _aidl_return);
}

ScopedAStatus Session::authenticateWithContext(
    int64_t operationId, const OperationContext &context,
    std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return authenticate(operationId, _aidl_return);
}

ScopedAStatus Session::detectInteractionWithContext(
    const OperationContext &context,
    std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return detectInteraction(_aidl_return);
}

ScopedAStatus Session::onContextChanged(const OperationContext &context) {
  LOG(INFO) << "Session::onContextChanged: reason=" << (int)context.reason
            << ", displayState=" << (int)context.displayState;
  return ScopedAStatus::ok();
}

void Session::postCallback(std::function<void()> task) {
  if (mCallbackQueueState) {
    std::lock_guard<std::mutex> lock(mCallbackQueueState->lock);
    if (mCallbackQueueState->running) {
      mCallbackQueueState->queue.push(std::move(task));
      mCallbackQueueState->cv.notify_all();
    }
  }
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
