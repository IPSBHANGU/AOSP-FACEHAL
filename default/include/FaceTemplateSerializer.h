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

#include <vector>
#include <cstdint>
#include <string>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

struct FaceTemplate {
    uint32_t version;
    int32_t faceId;
    int32_t userId;
    std::vector<uint8_t> templateData;
    uint32_t checksum;
    std::vector<uint8_t> mac; // Hardware-backed MAC
};

class FaceTemplateSerializer {
public:
    static uint32_t CURRENT_VERSION;

    // Serialize a FaceTemplate into a binary byte array
    static std::vector<uint8_t> serialize(const FaceTemplate& tmpl);

    // Deserialize a binary byte array into a FaceTemplate. Returns true if valid.
    static bool deserialize(const std::vector<uint8_t>& data, FaceTemplate& outTmpl);

    // Calculate a CRC32 or simple checksum
    static uint32_t calculateChecksum(const std::vector<uint8_t>& data);
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
