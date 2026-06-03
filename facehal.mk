#
# Copyright (C) 2026 The Project MiLahaina
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

LOCAL_PATH := $(call my-dir)

# Face HAL Service, Enrollment App, and Overlays
PRODUCT_PACKAGES += \
    android.hardware.biometrics.face-service.milahaina \
    MiLahainaVision \
    MiLahainaVisionOverlay

# Face HAL SELinux Policies
BOARD_VENDOR_SEPOLICY_DIRS += \
    $(LOCAL_PATH)/sepolicy

# Enable Logging
MILAHAINA_FACEHAL_ENABLE_LOGGING ?= false
$(call add_soong_config_namespace,milahaina_facehal)
$(call add_soong_config_var_value,milahaina_facehal,enable_logging,$(MILAHAINA_FACEHAL_ENABLE_LOGGING))

# Face Engine Model Type (milahaina or megvii)
MILAHAINA_FACEHAL_ENGINE_MODEL ?= milahaina

ifeq ($(MILAHAINA_FACEHAL_ENGINE_MODEL),megvii)
PRODUCT_SOONG_NAMESPACES += $(LOCAL_PATH)/lib/megvii
else
PRODUCT_SOONG_NAMESPACES += $(LOCAL_PATH)/lib/milahaina
endif
