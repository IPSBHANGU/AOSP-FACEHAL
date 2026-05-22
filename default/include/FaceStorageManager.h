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

#include "FaceTemplateSerializer.h"
#include "CryptoClient.h"
#include <string>
#include <vector>
#include <memory>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

/**
 * FaceStorageManager handles atomic file I/O, user isolation, and template
 * protection by combining CryptoClient and FaceTemplateSerializer.
 */
class FaceStorageManager {
public:
    FaceStorageManager();
    ~FaceStorageManager();

    // Setup base directories and permissions
    bool initialize();

    // Saves a face template. Falls back to an explicitly marked plaintext file
    // if Keystore2-backed encryption is unavailable.
    bool saveTemplate(const FaceTemplate& tmpl);

    // Loads a specific template. Returns true if valid, false if corrupted/missing.
    bool loadTemplate(int32_t userId, int32_t faceId, FaceTemplate& outTmpl);

    // Loads all valid templates for a user.
    std::vector<FaceTemplate> loadAllTemplatesForUser(int32_t userId);

    // Deletes a specific template.
    bool deleteTemplate(int32_t userId, int32_t faceId);

    // Wipes all on-disk face data for a user.
    bool removeUser(int32_t userId);

private:
    std::shared_ptr<CryptoClient> mCryptoClient;

    std::string getUserDirectory(int32_t userId);
    std::string getFilePath(int32_t userId, int32_t faceId);
    std::string getPlaintextFilePath(int32_t userId, int32_t faceId);

    // Reads a file into a byte array
    std::vector<uint8_t> readFile(const std::string& path);
};

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
