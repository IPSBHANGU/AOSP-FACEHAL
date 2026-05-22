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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <aidl/android/system/keystore2/IKeystoreService.h>
#include <aidl/android/system/keystore2/IKeystoreSecurityLevel.h>
#include <aidl/android/system/keystore2/KeyDescriptor.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {


using aidl::android::system::keystore2::IKeystoreService;
using aidl::android::system::keystore2::IKeystoreSecurityLevel;
using aidl::android::system::keystore2::KeyDescriptor;

class CryptoClient {
public:
    static std::shared_ptr<CryptoClient> getInstance();

    // Returns a secure random challenge. Returns 0 if the platform RNG fails.
    uint64_t generateChallenge();

    // Keystore2/KeyMint-backed storage crypto. These keys are hardware-backed
    // and owned by the HAL SELinux namespace, but they do not authorize
    // framework Keystore/FBE operations. Valid framework HardwareAuthTokens must come from a
    // trusted authenticator / Gatekeeper path; userspace HALs cannot fabricate
    std::vector<uint8_t> encrypt(int32_t userId, const std::vector<uint8_t>& data);
    std::vector<uint8_t> decrypt(int32_t userId, const std::vector<uint8_t>& ciphertext);

    // Keystore2/KeyMint-backed MAC for template integrity.
    std::vector<uint8_t> generateMac(const std::vector<uint8_t>& data);
    bool verifyMac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& mac);

private:
    CryptoClient();

    bool generateStorageKey(int32_t userId);
    bool generateMacKey();
    KeyDescriptor getStorageKeyDescriptor(int32_t userId) const;
    KeyDescriptor getMacKeyDescriptor() const;
    std::vector<uint8_t> signWithHmacKey(const std::vector<uint8_t>& data);

    std::shared_ptr<IKeystoreService> mKeystore;
    std::shared_ptr<IKeystoreSecurityLevel> mSecLevel;
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
