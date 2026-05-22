#pragma once

// Default to logging enabled unless overridden by Soong config.
#ifndef MILAHAINA_FACEHAL_ENABLE_LOGGING
#define MILAHAINA_FACEHAL_ENABLE_LOGGING 1
#endif

#if !MILAHAINA_FACEHAL_ENABLE_LOGGING
namespace org::milahaina::face::hal::logging {
class NullLogMessage {
 public:
  template <typename T>
  NullLogMessage& operator<<(const T&) {
    return *this;
  }
};
}  // namespace org::milahaina::face::hal::logging

#ifdef LOG
#undef LOG
#endif
#define LOG(severity) ::org::milahaina::face::hal::logging::NullLogMessage()

#ifdef ALOGV
#undef ALOGV
#endif
#define ALOGV(...) ((void)0)

#ifdef ALOGD
#undef ALOGD
#endif
#define ALOGD(...) ((void)0)

#ifdef ALOGI
#undef ALOGI
#endif
#define ALOGI(...) ((void)0)

#ifdef ALOGW
#undef ALOGW
#endif
#define ALOGW(...) ((void)0)

#ifdef ALOGE
#undef ALOGE
#endif
#define ALOGE(...) ((void)0)

#ifdef ALOGF
#undef ALOGF
#endif
#define ALOGF(...) ((void)0)
#endif
