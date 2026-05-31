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

#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>
#include "FaceEngineTypes.h"

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::AcquiredInfo;

class AcquiredInfoMapper {
public:
    static AcquiredInfo mapVendorCode(int res, int32_t &outVendorCode) {
        outVendorCode = res;
        switch (res) {
            case VendorCode::FACE_OK:
                return AcquiredInfo::GOOD;
            case VendorCode::KEEP:
                return AcquiredInfo::GOOD;
            case VendorCode::FAILED:
                return AcquiredInfo::INSUFFICIENT;
            case VendorCode::FACE_NOT_FOUND:
                return AcquiredInfo::NOT_DETECTED;
            case VendorCode::FACE_TOO_SMALL:
                return AcquiredInfo::TOO_FAR;
            case VendorCode::FACE_TOO_LARGE:
                return AcquiredInfo::TOO_CLOSE;
            case VendorCode::FACE_OFFSET_LEFT:
                return AcquiredInfo::FACE_TOO_RIGHT;
            case VendorCode::FACE_OFFSET_RIGHT:
                return AcquiredInfo::FACE_TOO_LEFT;
            case VendorCode::FACE_OFFSET_UP:
                return AcquiredInfo::FACE_TOO_LOW;
            case VendorCode::FACE_OFFSET_DOWN:
                return AcquiredInfo::FACE_TOO_HIGH;
            case VendorCode::FACE_ROTATED_LEFT:
            case VendorCode::FACE_ROTATED_RIGHT:
                return AcquiredInfo::PAN_TOO_EXTREME;
            case VendorCode::FACE_BLUR:
                return AcquiredInfo::TOO_MUCH_MOTION;
            case VendorCode::FACE_NOT_COMPLETE:
                return AcquiredInfo::FACE_OBSCURED;
            case VendorCode::DARKLIGHT:
                return AcquiredInfo::TOO_DARK;
            case VendorCode::HIGHLIGHT:
                return AcquiredInfo::TOO_BRIGHT;
            case VendorCode::HALF_SHADOW:
                return AcquiredInfo::INSUFFICIENT;
            case VendorCode::FACE_MULTI:
                return AcquiredInfo::INSUFFICIENT;
            default:
                outVendorCode = res;
                return AcquiredInfo::VENDOR;
        }
    }
};

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
