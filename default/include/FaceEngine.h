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
#include "FaceStorageCallbacks.h"
#include "VendorStateManager.h"
#include <aidl/android/hardware/biometrics/face/SensorProps.h>
#include <memory>
#include <mutex>
#include <string>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

class FaceEngine {
public:
  static FaceEngine &getInstance();

  void getSensorProps(aidl::android::hardware::biometrics::face::SensorProps &props);

  bool init(const FaceEngineCallbacks &callbacks);
  void release();

  int authenticate(const std::vector<uint8_t> &nv21Frame, int width, int height,
                   int userId, float &outScore, int32_t &outFaceId);

  int enroll(int userId, const std::vector<uint8_t> &nv21Frame, int width,
             int height, int32_t &outFaceId);

  int analyzeFaceQuality(const std::vector<uint8_t> &nv21, int width,
                         int height);

  void requestCapture() { mCaptureRequested = true; }

  int deleteEnrollment(int userId, int faceId);

  int getEnrollmentCount(int userId);
  std::vector<int32_t> getEnrolledFaceIds(int userId);

  int getEnrollmentProgress() {
    return (mEnrollFrameCount * 100) / ENROLL_REQUIRED_GOOD_FRAMES;
  }

  int mEnrollFrameCount = 0;
  static constexpr int ENROLL_REQUIRED_GOOD_FRAMES = 5;

  bool restoreEnrollments(int userId);

  void setVisionFrameCallback(
      std::function<int(const std::vector<uint8_t> &, int, int, int)> cb) {
    mVisionFrameCallback = std::move(cb);
  }

  void setSessionFrameCallback(
      std::function<int(const std::vector<uint8_t> &, int, int, int)> cb) {
    mSessionFrameCallback = std::move(cb);
  }

  int onCameraFrame(const std::vector<uint8_t> &frame, int width, int height,
                    int angle) {
    mRotation = angle;
    if (mVisionFrameCallback) {
      mVisionFrameCallback(frame, width, height, angle);
    }
    if (mSessionFrameCallback) {
      return mSessionFrameCallback(frame, width, height, angle);
    }
    return -1;
  }

  std::vector<float> getLastLandmarks();

  void setRequireAttention(bool enabled) { mRequireAttention = enabled; }
  bool getRequireAttention() const { return mRequireAttention; }

  void setRequireDiversePoses(bool enabled) { mRequireDiversePoses = enabled; }
  bool getRequireDiversePoses() const { return mRequireDiversePoses; }

  void resetEnrollment();

  enum class FacePose { CENTER, LEFT, RIGHT, UP, DOWN };

  int getLastVendorCode() const { return VendorStateManager::getInstance().getVendorCode(); }

  int getRotation() const { return mRotation; }

private:
  FaceEngine();
  ~FaceEngine();

  struct Impl;
  std::unique_ptr<Impl> mImpl;

  bool mRequireAttention = true;
  bool mRequireDiversePoses = false;
  bool mCaptureRequested = false;
  int mRotation = 0;

  std::function<int(const std::vector<uint8_t>&, int, int, int)>
      mVisionFrameCallback;
  std::function<int(const std::vector<uint8_t>&, int, int, int)>
      mSessionFrameCallback;
};

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org

