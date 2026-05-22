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

#include "FaceEngineTypes.h"
#include "FaceStorageManager.h"
#include "FaceTemplateSerializer.h"
#include "VendorStateManager.h"
#include <android-base/logging.h>
#include <cstring>
#include <vector>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

inline FaceEngineCallbacks getFaceEngineCallbacks() {
  static FaceStorageManager storageManager;
  FaceEngineCallbacks callbacks;
  callbacks.saveTemplate = [](int userId, int faceId, const std::vector<float>& embedding) -> bool {
    FaceTemplate tmpl;
    tmpl.version = FaceTemplateSerializer::CURRENT_VERSION;
    tmpl.userId = userId;
    tmpl.faceId = faceId;
    tmpl.templateData.resize(embedding.size() * sizeof(float));
    std::memcpy(tmpl.templateData.data(), embedding.data(), embedding.size() * sizeof(float));
    return storageManager.saveTemplate(tmpl);
  };
  callbacks.deleteTemplate = [](int userId, int faceId) -> bool {
    storageManager.deleteTemplate(userId, faceId);
    return true;
  };
  callbacks.loadTemplates = [](int userId) -> std::vector<std::pair<int32_t, std::vector<float>>> {
    std::vector<std::pair<int32_t, std::vector<float>>> result;
    auto templates = storageManager.loadAllTemplatesForUser(userId);
    for (const auto& tmpl : templates) {
        if (tmpl.version != FaceTemplateSerializer::CURRENT_VERSION ||
            tmpl.templateData.size() != 512 * sizeof(float)) {
          LOG(WARNING) << "Outdated or invalid template found for face " << tmpl.faceId << ". Deleting.";
          storageManager.deleteTemplate(userId, tmpl.faceId);
          VendorStateManager::getInstance().setVendorCode(VendorCode::REQUIRE_REENROLLMENT);
        } else {
        std::vector<float> embedding(512);
        std::memcpy(embedding.data(), tmpl.templateData.data(), 512 * sizeof(float));
        result.push_back({tmpl.faceId, std::move(embedding)});
      }
    }
    return result;
  };
  return callbacks;
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
