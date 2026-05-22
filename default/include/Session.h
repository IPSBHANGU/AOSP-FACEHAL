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

#pragma once

#include <aidl/android/hardware/biometrics/face/BnSession.h>
#include <aidl/android/hardware/biometrics/face/ISessionCallback.h>
#include <aidl/android/hardware/biometrics/face/Error.h>
#include <aidl/android/hardware/common/NativeHandle.h>

#include "FaceEngine.h"
#include "CryptoClient.h"
#include "CameraClient.h"

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::BnSession;
using aidl::android::hardware::biometrics::face::ISessionCallback;
using aidl::android::hardware::keymaster::HardwareAuthToken;
using aidl::android::hardware::biometrics::common::ICancellationSignal;
using aidl::android::hardware::biometrics::common::OperationContext;
using aidl::android::hardware::biometrics::face::EnrollmentStageConfig;
using aidl::android::hardware::biometrics::face::EnrollmentType;
using aidl::android::hardware::biometrics::face::Feature;
using aidl::android::hardware::biometrics::face::FaceEnrollOptions;
using aidl::android::hardware::common::NativeHandle;
using ndk::ScopedAStatus;

class Session : public BnSession {
public:
    Session(int32_t userId, const std::shared_ptr<ISessionCallback>& cb);
    ~Session();

    ScopedAStatus generateChallenge(void) override;
    ScopedAStatus revokeChallenge(int64_t challenge) override;
    ScopedAStatus getEnrollmentConfig(EnrollmentType enrollmentType, std::vector<EnrollmentStageConfig>* _aidl_return) override;
    ScopedAStatus enroll(const HardwareAuthToken& hat, EnrollmentType type,
                         const std::vector<Feature>& features,
                         const std::optional<NativeHandle>& previewSurface,
                         std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus enrollWithOptions(const FaceEnrollOptions& options,
                                    std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus authenticate(int64_t operationId,
                               std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus detectInteraction(std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus enumerateEnrollments(void) override;
    ScopedAStatus removeEnrollments(const std::vector<int32_t>& enrollmentIds) override;
    ScopedAStatus getFeatures() override;
    ScopedAStatus setFeature(const HardwareAuthToken& hat, Feature feature, bool enabled) override;
    ScopedAStatus getAuthenticatorId(void) override;
    ScopedAStatus invalidateAuthenticatorId(void) override;
    ScopedAStatus resetLockout(const HardwareAuthToken& hat) override;
    ScopedAStatus close(void) override;
    ScopedAStatus enrollWithContext(const HardwareAuthToken& hat, EnrollmentType type,
                                    const std::vector<Feature>& features,
                                    const std::optional<NativeHandle>& previewSurface,
                                    const OperationContext& context,
                                    std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus authenticateWithContext(int64_t operationId, const OperationContext& context,
                                          std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus detectInteractionWithContext(const OperationContext& context,
                                               std::shared_ptr<ICancellationSignal>* _aidl_return) override;
    ScopedAStatus onContextChanged(const OperationContext& context) override;

    void cancel();

private:
    int32_t mUserId;
    std::shared_ptr<ISessionCallback> mCb;

    FaceEngine& mEngine;
    std::shared_ptr<CryptoClient> mCryptoClient;
    std::shared_ptr<CameraClient> mCameraClient;

    bool mIsAuthenticating;
    bool mIsEnrolling;
    bool mIsDetectingInteraction;
    int32_t mEnrollRemaining;
    uint64_t mCurrentChallenge;

    int onCameraFrame(const std::vector<uint8_t>& frame, int width, int height, int angle);
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
