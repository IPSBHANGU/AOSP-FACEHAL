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

#include <atomic>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

/**
 * Thread-safe Singleton to hold the current FaceEngine Vendor Code.
 * Ensures critical asynchronous codes (like REQUIRE_REENROLLMENT)
 * are accessible throughout the HAL and persist across sessions until consumed.
 */
class VendorStateManager {
public:
    static VendorStateManager& getInstance() {
        static VendorStateManager instance;
        return instance;
    }

    void setVendorCode(int code) {
        mCurrentVendorCode.store(code, std::memory_order_relaxed);
    }

    int consumeVendorCode() {
        return mCurrentVendorCode.exchange(0, std::memory_order_relaxed); // 0 = FACE_OK
    }

    int getVendorCode() const {
        return mCurrentVendorCode.load(std::memory_order_relaxed);
    }

private:
    VendorStateManager() : mCurrentVendorCode(0) {}
    ~VendorStateManager() = default;

    // Delete copy/move constructors to enforce singleton
    VendorStateManager(const VendorStateManager&) = delete;
    VendorStateManager& operator=(const VendorStateManager&) = delete;

    std::atomic<int> mCurrentVendorCode;
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
