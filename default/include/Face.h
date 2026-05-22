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

#include <aidl/android/hardware/biometrics/face/BnFace.h>
#include <aidl/android/hardware/biometrics/face/ISession.h>
#include <aidl/android/hardware/biometrics/face/SensorProps.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::BnFace;
using aidl::android::hardware::biometrics::face::ISession;
using aidl::android::hardware::biometrics::face::ISessionCallback;
using aidl::android::hardware::biometrics::face::SensorProps;
using ndk::ScopedAStatus;

class Face : public BnFace {
public:
    Face();
    ScopedAStatus getSensorProps(std::vector<SensorProps>* _aidl_return) override;
    ScopedAStatus createSession(int32_t sensorId, int32_t userId,
                                const std::shared_ptr<ISessionCallback>& cb,
                                std::shared_ptr<ISession>* _aidl_return) override;

private:
    std::vector<SensorProps> mSensorProps;
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
