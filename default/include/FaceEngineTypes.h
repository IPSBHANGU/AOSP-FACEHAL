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
#include <functional>
#include <vector>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

struct VendorCode {
  static constexpr int FACE_OK = 0;
  static constexpr int FAILED = 3;
  static constexpr int FACE_NOT_FOUND = 5;
  static constexpr int FACE_TOO_SMALL = 6;
  static constexpr int FACE_TOO_LARGE = 7;
  static constexpr int FACE_OFFSET_LEFT = 8;
  static constexpr int FACE_OFFSET_RIGHT = 10;
  static constexpr int FACE_OFFSET_UP = 11;
  static constexpr int FACE_OFFSET_DOWN = 12;
  static constexpr int FACE_ROTATED_LEFT = 15;
  static constexpr int FACE_ROTATED_RIGHT = 17;
  static constexpr int KEEP = 19;
  static constexpr int FACE_MULTI = 27;
  static constexpr int FACE_BLUR = 28;
  static constexpr int FACE_NOT_COMPLETE = 29;
  static constexpr int DARKLIGHT = 30;
  static constexpr int HIGHLIGHT = 31;
  static constexpr int HALF_SHADOW = 32;

  static constexpr int CAMERA_NO_DEVICE = 50;
  static constexpr int CAMERA_ID_SELECT_FAILED = 51;
  static constexpr int CAMERA_MANAGER_FAILED = 52;
  static constexpr int CAMERA_OPEN_FAILED = 53;
  static constexpr int CAMERA_IMAGE_READER_FAILED = 54;
  static constexpr int CAMERA_WINDOW_FAILED = 55;
  static constexpr int CAMERA_SESSION_FAILED = 56;
  static constexpr int CAMERA_REQUEST_FAILED = 57;
  static constexpr int CAMERA_STREAMING_FAILED = 58;
  static constexpr int REQUIRE_REENROLLMENT = 1059;
};

struct FaceEngineCallbacks {
  std::function<bool(int userId, int faceId,
                     const std::vector<float> &embedding)>
      saveTemplate;
  std::function<bool(int userId, int faceId)> deleteTemplate;
  std::function<std::vector<std::pair<int32_t, std::vector<float>>>(int userId)>
      loadTemplates;
};

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
