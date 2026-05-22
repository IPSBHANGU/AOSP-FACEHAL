# MiLahaina Face HAL

This repository contains the biometric Face Hardware Abstraction Layer (HAL) implementation for the MiLahaina project, supporting Android's stable AIDL interfaces (`android.hardware.biometrics.face`).

> [!NOTE]
> This is a hobby project shared/made for educational and experimentation purposes. No one has any obligation to use it. You have full access to the source code and can design/integrate your own face unlock logic.

---

## How It Works: High-Level Architecture

The MiLahaina Face HAL is designed with a modular separation of concerns between open-source framework glue and proprietary/compiled biometric engines.

```
                  +-----------------------------------------+
                  |  Android System (Biometrics Framework)  |
                  +--------------------+--------------------+
                                       | (AIDL Calls)
                                       v
                  +--------------------+--------------------+
                  |    android.hardware.biometrics.face     |
                  |          HAL Daemon (Service)           |
                  +--------------------+--------------------+
                                       |
                    (Statically links libmilahaina_facehal_core)
                                       v
                  +--------------------+--------------------+
                  |     libmilahaina_facehal_core (Static)  |
                  |  - Session management & camera pipeline |
                  |  - Keystore2 & template encryption      |
                  +--------------------+--------------------+
                                       |
                    (Dynamically links libmilahaina_face_engine.so)
                                       v
                  +--------------------+--------------------+
                  |      libmilahaina_face_engine (Shared)  |
                  |  - Receives storage callbacks at init   |
                  |  - Resolves landmarks & face scores     |
                  |  - [Stub] or [Proprietary Engine]       |
                  +-----------------------------------------+
```

1. **Service Daemon & Core HAL (`libmilahaina_facehal_core`)**:
   - Manages the biometric session lifecycle (enrollment, authentication, cancellation).
   - Operates the NDK Camera2 backend to capture incoming video frames.
   - Manages cryptographic keys via Android Keystore2 and handles persistent database storage/encryption.

2. **Face Engine Interface (`libmilahaina_face_engine.so`)**:
   - Acts as the main brain logic of the face-unlock pipeline (responsible for face detection, quality analysis, embedding extraction, and matching).
   - The core HAL registers functional callbacks (`FaceEngineCallbacks`) during initialization. Whenever the engine needs to save, load, or delete face embeddings, it invokes these callbacks.
   - Exposes a clean C++ public interface utilizing the **Pimpl (Pointer to Implementation)** pattern, hiding execution details from compiling clients.

3. **Vision Service Interface (`vendor.milahaina.biometrics.face.IVisionService`)**:
   - A custom AIDL interface hosted by the HAL daemon alongside the standard Android `IFace` biometric interface.
   - Purpose: It serves as the framework bridge that allows system/platform application clients (such as the `FaceUnlock` app) to register callbacks and securely receive real-time camera frames and calculated landmark/pose coordinates.
   - Utilizes Ashmem (shared memory buffers) to transfer high-resolution frames efficiently, enabling fluid camera previews and face mesh animations during enrollment without blocking the core biometric pipeline.

---

## Build System Configuration (Stub vs. Proprietary)

The compilation behavior is governed by the Android Soong build configuration namespace `milahaina_facehal` and the modules defined in `lib/Android.bp`.

### 1. Prebuilt Proprietary Mode (Default)
By default, the repository targets the prebuilt proprietary shared library (`lib/libmilahaina_face_engine.so`) using the `cc_prebuilt_library_shared` module in `lib/Android.bp`. This contains the fully optimized vendor engine with actual biometric algorithms.

### 2. Open-Source Stub Mode
If you wish to compile the engine from source using the open-source dummy stub (`lib/FaceEngineStub.cpp`) containing dummy biometric calls:
1. Open `lib/Android.bp` and comment out the `cc_prebuilt_library_shared` module for `libmilahaina_face_engine`.
2. Uncomment the `cc_library_shared` module for `libmilahaina_face_engine` that lists `FaceEngineStub.cpp` as a source.
3. Build the targets from your Android build environment root:
   ```bash
   m libmilahaina_face_engine
   m android.hardware.biometrics.face-service.milahaina
   ```

---

## Cryptographic Architecture & Secure Storage

To satisfy Android biometrics security requirements, user enrollment templates are serialized, signed, and encrypted before being written to `/data/vendor/biometrics/face/`.

### 1. Serialization Protocol (`FaceTemplateSerializer.cpp`)
Biometric templates are serialized into a binary stream containing:
- `uint32_t` Version
- `int32_t` Face ID
- `int32_t` User ID
- `uint32_t` Template Size
- `uint8_t[]` Template Raw Embedding Data
- `uint32_t` FNV-1a Checksum (calculated over the preceding fields)

### 2. Integrity Protection (KeyMint HMAC-SHA256)
To detect offline tampering, the serialized binary is signed using a hardware-backed HMAC key.
- **Key Alias**: `milahaina_face_template_hmac_sha256_v2`
- **Namespace**: `65000` (Domain: `SELINUX`)
- **Key Type**: 256-bit HMAC key generated and managed inside KeyMint.
- The 256-bit signature (32 bytes) is appended directly to the end of the serialized payload. During deserialization, the MAC signature is verified prior to parsing.

### 3. Data Confidentiality (KeyMint AES-GCM-256)
The MAC-signed serialized payload is encrypted using hardware-backed AES-GCM-256 keys generated per Android user.
- **Key Alias**: `milahaina_face_template_aes_gcm_v2_user_<userId>`
- **Namespace**: `65000` (Domain: `SELINUX`)
- **Mode**: AES-GCM with a random 96-bit nonce and 128-bit authentication tag.
- The encrypted output is formatted as a versioned storage payload:
  `[1-byte Version] [1-byte Nonce Length] [Nonce Bytes...] [Ciphertext + Auth Tag Bytes...]`

### 4. Non-Encrypted Plaintext Fallback
If KeyMint/Keystore2 services are unavailable (e.g. during disabled verity/encryption), the core HAL will gracefully fall back to storing only the MAC-signed, checksum-validated serialization stream.
- **Path**: `/data/vendor/biometrics/face/user_<userId>/face_<faceId>_without_encryption.dat`
- **Security Warning**: Although templates stored in this fallback path are still protected against tampering via the hardware-backed HMAC signature, they are not encrypted. As soon as Keystore2 becomes available and a new enrollment session is started, fallback files are processed, re-encrypted, and the unencrypted file versions are securely wiped.

---

## State Management & Custom Error Propagation

The HAL utilizes a thread-safe singleton, `VendorStateManager`, to coordinate asynchronous event states across multiple system boundaries.

### Asynchronous Vendor Codes
Standard AIDL `IFace` sessions communicate status frames. When non-standard events occur, the HAL propagates them using custom vendor codes (e.g., to trigger specific user interface prompts):
- `VendorCode::REQUIRE_REENROLLMENT` (`1059`): Signals the framework/app that the stored template version has changed or is incompatible, requiring a user re-enrollment prompt.
- `VendorCode::DARKLIGHT` (`30`), `VendorCode::HIGHLIGHT` (`31`), `VendorCode::HALF_SHADOW` (`32`): Camera/ambient lighting state quality feedback.
- `VendorCode::CAMERA_NO_DEVICE` (`50`), `VendorCode::CAMERA_OPEN_FAILED` (`53`): Low-level camera acquisition/configuration failures.

---

## IVisionService & Landmark Visualization

The Face HAL exposes a vendor-specific AIDL interface alongside standard biometrics APIs: `vendor.milahaina.biometrics.face.IVisionService`.

### 1. High-Performance Preview Delivery (Ashmem)
Since raw high-resolution NV21 frames are too large to pass over Binder IPC, `IVisionService` uses Android Shared Memory (Ashmem):
1. The camera capture thread writes NV21 frames directly into an ashmem region.
2. The HAL passes a read-only `ParcelFileDescriptor` to client applications.
3. The client maps the descriptor to instantly access frame bytes with zero copy overhead, maintaining high framerates for UI rendering.

### 2. Face Landmarks Coordinate Layout
The interface method `getLastLandmarks()` returns a 13-element float array representing the key coordinates calculated from the last successful face detection frame.
- **Coordinate format**: `[score, right_eye_x, right_eye_y, left_eye_x, left_eye_y, nose_x, nose_y, mouth_x, mouth_y, right_ear_x, right_ear_y, left_ear_x, left_ear_y]`
- **Normalization**: All coordinates are mapped to the `[0.0, 1.0]` range relative to the frame dimensions.
- **Fallback**: Returns an empty array if no face is detected in the frame.

---

## SELinux / SEPolicy Configuration Reference

Below is a snapshot of the SELinux rules added to allow the Face HAL to interact with cameras, keystore, graphics memory, and the DSP backend:

### `file_contexts`
```sepolicy
/vendor/bin/hw/android\.hardware\.biometrics\.face-service\.milahaina u:object_r:hal_milahaina_biometrics_face_exec:s0
/data/vendor/biometrics/face(/.*)?                                  u:object_r:hal_milahaina_biometrics_face_data_file:s0
```

### `hal_milahaina_biometrics_face.te`
```sepolicy
type hal_milahaina_biometrics_face, domain;
type hal_milahaina_biometrics_face_exec, exec_type, vendor_file_type, file_type;
type hal_milahaina_biometrics_face_data_file, file_type, data_file_type;
type hal_milahaina_biometrics_face_keystore2_key, keystore2_key_type;

init_daemon_domain(hal_milahaina_biometrics_face)

hal_server_domain(hal_milahaina_biometrics_face, hal_face)

r_dir_file(hal_milahaina_biometrics_face, adsprpcd_file)

#  DMA buf access
allow hal_milahaina_biometrics_face vendor_dmabuf_system_heap_device:chr_file r_file_perms;
allow hal_milahaina_biometrics_face vendor_dmabuf_user_contig_heap_device:chr_file r_file_perms;
allow hal_milahaina_biometrics_face vendor_dmabuf_secure_cdsp_heap_device:chr_file r_file_perms;

#  Access for qseecom_memory
allow hal_milahaina_biometrics_face tee_device:chr_file rw_file_perms;

#  Access for DSP/QDSP device
allow hal_milahaina_biometrics_face vendor_qdsp_device:chr_file rw_file_perms;
allow hal_milahaina_biometrics_face vendor_dsp_device:chr_file rw_file_perms;

#Access for graphics buffer
hal_client_domain(hal_milahaina_biometrics_face, hal_graphics_allocator);

typeattribute hal_milahaina_biometrics_face hal_camera_client;

# Allow reading vendor.face.camera properties
get_prop(hal_milahaina_biometrics_face, vendor_camera_prop);

# Allow to read persist.biometrics.face3d.producer,adsprpc prop
get_prop(hal_milahaina_biometrics_face, vendor_face3d_producer_prop);
get_prop(hal_milahaina_biometrics_face, vendor_adsprpc_prop);

allow hal_milahaina_biometrics_face fwk_camera_hwservice:hwservice_manager find;
allow hal_milahaina_biometrics_face hidl_token_hwservice:hwservice_manager find;

dontaudit hal_milahaina_biometrics_face system_file:file *;

# Allow access to camera service for NDK Camera API
allow hal_milahaina_biometrics_face cameraserver_service:service_manager find;
binder_call(hal_milahaina_biometrics_face, cameraserver)

# Allow access to AIDL Camera Provider
allow hal_milahaina_biometrics_face fwk_camera_service:service_manager find;
allow hal_milahaina_biometrics_face hal_camera_service:service_manager find;
binder_call(hal_milahaina_biometrics_face, hal_camera_server)
binder_call(hal_milahaina_biometrics_face, hal_camera_default)

# Allow access to Keystore2
allow hal_milahaina_biometrics_face keystore_service:service_manager find;
binder_call(hal_milahaina_biometrics_face, keystore)
allow hal_milahaina_biometrics_face hal_milahaina_biometrics_face_keystore2_key:keystore2_key { get_info rebind use };

# Allow binder calls to the vision app for callbacks
binder_call(hal_milahaina_biometrics_face, platform_app)

# Allow reading camera configuration properties
get_prop(hal_milahaina_biometrics_face, camera_config_prop)

#  Access for /data/vendor/biometrics/face
allow hal_milahaina_biometrics_face hal_milahaina_biometrics_face_data_file:dir create_dir_perms;
allow hal_milahaina_biometrics_face hal_milahaina_biometrics_face_data_file:file create_file_perms;

# Allow reading model files from /vendor/etc/face
r_dir_file(hal_milahaina_biometrics_face, vendor_configs_file)

# Allow NNAPI delegate to talk to Neural Networks HAL (Hexagon DSP)
hal_client_domain(hal_milahaina_biometrics_face, hal_neuralnetworks)
```

### `keystore2_key_contexts`
```sepolicy
65000 u:object_r:hal_milahaina_biometrics_face_keystore2_key:s0
```

### `platform_app.te`
```sepolicy
allow platform_app tmpfs:file read;
```

### `service_contexts`
```sepolicy
vendor.milahaina.biometrics.face.IVisionService/default u:object_r:vendor_vision_service:s0
```

### `vendor_vision_service.te`
```sepolicy
# Service type for vendor.milahaina.biometrics.face.IVisionService
type vendor_vision_service, service_manager_type, hal_service_type;

# Allow hal_milahaina_biometrics_face domain to register IVisionService with ServiceManager
allow hal_milahaina_biometrics_face vendor_vision_service:service_manager { add find };

# Allow FaceUnlock app (platform_app / system_app) to find and call IVisionService
allow platform_app vendor_vision_service:service_manager find;
binder_call(platform_app, hal_milahaina_biometrics_face)

allow system_app vendor_vision_service:service_manager find;
binder_call(system_app, hal_milahaina_biometrics_face)
```

---

## Implementing a Custom Face Engine

To implement your own face unlock engine, you can replace the stub library (`FaceEngineStub.cpp`) with your own custom logic.

Because the architecture decouples core system tasks (like camera capture, database serialization, and Keystore2 encryption) from the raw face recognition algorithms, your custom engine does not need to handle database storage, hashing, or crypto keys. Instead, your engine only needs to handle face detection, quality analysis, embedding extraction, and similarity comparisons.

### 1. The FaceEngineCallbacks System
During initialization, the core HAL registers callbacks (`FaceEngineCallbacks`) that decouple the engine from the database and encryption layers:
- `callbacks.saveTemplate(userId, faceId, embedding)`: Saves a float vector embedding to secure storage.
- `callbacks.loadTemplates(userId)`: Loads all templates for a user (returning a list of `pair<int32_t, vector<float>>`).
- `callbacks.deleteTemplate(userId, faceId)`: Deletes a specific template from secure storage.

### 2. Implementation Template (`lib/FaceEngineCustom.cpp`)
Create your custom implementation file implementing the interface in `include/FaceEngine.h`. You can use the **Pimpl (Pointer to Implementation)** pattern to keep your internal engine state private:

```cpp
#include "FaceEngine.h"
#include <android-base/logging.h>
#include <map>
#include <mutex>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

struct FaceEngine::Impl {
    FaceEngineCallbacks mCallbacks;
    std::map<int32_t, std::vector<float>> enrolledEmbeddings;
    std::mutex enrollmentMutex;
};

FaceEngine &FaceEngine::getInstance() {
    static FaceEngine instance;
    return instance;
}

FaceEngine::FaceEngine() : mImpl(std::make_unique<Impl>()) {}
FaceEngine::~FaceEngine() = default;

bool FaceEngine::init(const FaceEngineCallbacks &callbacks) {
    mImpl->mCallbacks = callbacks;
    LOG(INFO) << "Custom FaceEngine initialized";
    return true;
}

void FaceEngine::release() {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
}

bool FaceEngine::restoreEnrollments(int userId) {
    if (!mImpl->mCallbacks.loadTemplates) {
        return false;
    }
    auto loaded = mImpl->mCallbacks.loadTemplates(userId);
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
    for (auto &pair : loaded) {
        mImpl->enrolledEmbeddings[pair.first] = std::move(pair.second);
    }
    return true;
}

int FaceEngine::enroll(int userId, const std::vector<uint8_t> &nv21Frame, int width, int height, int32_t &outFaceId) {
    // 1. Run quality logic & pose detection (analyze lighting, size, etc.)
    // 2. Extract float embedding vector (e.g., 512 dimensions)
    std::vector<float> embedding(512, 0.0f); // Compute real embedding here
    
    // 3. Generate new face ID
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    int32_t maxId = 0;
    for (const auto &[faceId, _] : mImpl->enrolledEmbeddings) {
        if (faceId > maxId) maxId = faceId;
    }
    outFaceId = maxId + 1;
    
    // 4. Save to secure storage via callback
    if (mImpl->mCallbacks.saveTemplate) {
        if (!mImpl->mCallbacks.saveTemplate(userId, outFaceId, embedding)) {
            LOG(ERROR) << "Failed to save enrollment template";
            return VendorCode::FAILED;
        }
    }
    
    mImpl->enrolledEmbeddings[outFaceId] = std::move(embedding);
    return VendorCode::FACE_OK;
}

int FaceEngine::authenticate(const std::vector<uint8_t> &nv21Frame, int width, int height, int userId, float &outScore, int32_t &outFaceId) {
    // 1. Extract live embedding
    std::vector<float> liveEmbedding = { /* ... extracted features ... */ };
    
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    float bestScore = -1.0f;
    int32_t matchedId = -1;
    
    // 2. Compare against cached templates
    for (const auto &[faceId, templateEmbedding] : mImpl->enrolledEmbeddings) {
        float score = 0.0f; // Calculate similarity (e.g. Cosine Similarity)
        for (size_t i = 0; i < liveEmbedding.size(); ++i) {
             score += liveEmbedding[i] * templateEmbedding[i];
        }
        if (score > bestScore) {
             bestScore = score;
             matchedId = faceId;
        }
    }
    
    outScore = bestScore;
    outFaceId = matchedId;
    
    // Threshold comparison
    return (bestScore >= 0.75f) ? 0 : 1;
}

int FaceEngine::analyzeFaceQuality(const std::vector<uint8_t> &nv21, int width, int height) {
    return VendorCode::FACE_OK;
}

int FaceEngine::deleteEnrollment(int userId, int faceId) {
    if (mImpl->mCallbacks.deleteTemplate) {
        mImpl->mCallbacks.deleteTemplate(userId, faceId);
    }
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.erase(faceId);
    return 0;
}

int FaceEngine::getEnrollmentCount(int userId) {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    return mImpl->enrolledEmbeddings.size();
}

std::vector<float> FaceEngine::getLastLandmarks() {
    return {};
}

void FaceEngine::resetEnrollment() {
    mEnrollFrameCount = 0;
}

void FaceEngine::getSensorProps(aidl::android::hardware::biometrics::face::SensorProps &props) {
    // Populate sensor configuration info
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
```

### 3. Integrating with `lib/Android.bp`
Update the target compiler rules to build the custom source file (e.g. by commenting out the prebuilt shared target and using `cc_library_shared` module):
```bp
cc_library_shared {
    name: "libmilahaina_face_engine",
    vendor: true,
    header_libs: ["libmilahaina_facehal_headers"],
    export_header_lib_headers: ["libmilahaina_facehal_headers"],
    srcs: [
        "FaceEngineCustom.cpp",
    ],
    shared_libs: [
        "android.hardware.biometrics.face-V4-ndk",
        "android.hardware.biometrics.common-V4-ndk",
        "libbase",
        "libcrypto",
        "liblog",
        // Add any libraries your engine requires
    ],
}
```

---

## Creating a Custom Enrollment App (FaceUnlock)

The Face HAL does not draw any UI on screen. Instead, Android launches a vendor-specific enrollment activity defined in the system configurations overlay. 

Here is how to configure, register, and implement a custom enrollment application (`FaceUnlock`) to display the enrollment UI, process camera previews, and bind with the HAL's `IVisionService`.

### 1. Registering the Activity in System Overlay
To tell the Android Settings app to launch your custom activity instead of the default settings enrollment flow, override the following config value in your device tree overlay:

**File**: `device/vendor/YOUR_DEVICE/overlay/packages/apps/Settings/res/values/config.xml`
```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <!-- ComponentName to launch a vendor-specific enrollment activity if available -->
    <string name="config_face_enroll" translatable="false">com.your.faceunlockapp/com.your.faceunlockapp.activities.EnrollActivity</string>
</resources>
```

### 2. AndroidManifest.xml Configuration
The enrollment activity must declare permissions for using the biometrics/face manager and must have an intent-filter to capture settings enrollment intents.

**File**: `AndroidManifest.xml`
```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.your.faceunlockapp">

    <!-- Permissions required to use biometrics and camera services -->
    <uses-permission android:name="android.permission.USE_BIOMETRIC" />
    <uses-permission android:name="android.permission.MANAGE_BIOMETRIC" />
    <uses-permission android:name="android.permission.CAMERA" />

    <application
        android:label="FaceUnlock"
        android:theme="@android:style/Theme.DeviceDefault.NoActionBar">

        <activity
            android:name=".activities.EnrollActivity"
            android:exported="true"
            android:screenOrientation="portrait">
            <intent-filter>
                <action android:name="android.settings.FACE_ENROLL" />
                <category android:name="android.intent.category.DEFAULT" />
            </intent-filter>
        </activity>

    </application>
</manifest>
```

### 3. Android.bp Configuration
To compile the application as a system-privileged vendor app with Platform certificate signing, use the following Soong module configuration.

**File**: `Android.bp`
```bp
android_app {
    name: "FaceUnlock",
    srcs: ["src/**/*.java", "src/**/*.kt"],
    resource_dirs: ["res"],
    static_libs: [
        "androidx.core_core",
        "androidx.appcompat_appcompat",
        "setup-wizard-lib",
        "setupcompat",
        "setupdesign",
        "vendor.milahaina.biometrics.face-V1-java", // AIDL interface Java bindings
    ],
    sdk_version: "system_current",
    privileged: true,
    certificate: "platform",
    optimize: {
        enabled: false,
    },
    vendor: true,
}
```

### 4. Layout XML Configuration (`face_enroll.xml`)
The enrollment activity UI layout using standard `GlifLayout` containing the custom `CircleSurfaceView` for camera preview and a status message text view.

**File**: `res/layout/face_enroll.xml`
```xml
<?xml version="1.0" encoding="utf-8"?>
<com.google.android.setupdesign.GlifLayout
    xmlns:android="http://schemas.android.com/apk/res/android"
    android:icon="@drawable/ic_face_header"
    android:id="@+id/face_enroll"
    android:layout_width="match_parent"
    android:layout_height="match_parent">

    <LinearLayout
        android:orientation="vertical"
        android:clipChildren="false"
        android:clipToPadding="false"
        android:layout_width="match_parent"
        android:layout_height="match_parent"
        style="@style/SudContentFrame">

        <LinearLayout
            android:gravity="center"
            android:orientation="vertical"
            android:clipChildren="false"
            android:clipToPadding="false"
            android:layout_width="match_parent"
            android:layout_height="0dp"
            android:layout_weight="1">

            <LinearLayout
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:orientation="vertical">

                <FrameLayout
                    android:id="@+id/surface_view"
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content"
                    android:layout_marginTop="48dp"
                    android:gravity="center_horizontal">

                    <!-- Custom Circle Surface View for rendering incoming camera frames -->
                    <com.your.faceunlockapp.view.CircleSurfaceView
                        android:id="@+id/camera_surface"
                        android:layout_width="240.0dip"
                        android:layout_height="320.0dip"
                        android:layout_gravity="center_horizontal"
                        android:background="#00000000" />

                </FrameLayout>

                <FrameLayout
                    android:layout_width="match_parent"
                    android:layout_height="wrap_content">
                    <TextView
                        android:ellipsize="end"
                        android:gravity="center"
                        android:layout_gravity="center_horizontal"
                        android:id="@+id/face_vendor_message"
                        android:layout_width="wrap_content"
                        android:layout_height="wrap_content"
                        android:layout_marginTop="12dp"
                        android:lines="3"
                        android:accessibilityLiveRegion="polite"
                        style="@style/TextAppearance.FaceErrorText"/>
                </FrameLayout>
            </LinearLayout>
        </LinearLayout>
    </LinearLayout>
</com.google.android.setupdesign.GlifLayout>
```

### 5. Copy-Paste Demo Activity (`EnrollActivity.kt`)
Below is a complete, self-contained implementation of `EnrollActivity.kt`. It binds to `IVisionService`, receives camera frames from the HAL, decodes them to show on screen, handles progress callbacks from `FaceManager`, and updates a face mesh overlay with calculated landmarks.

```kotlin
package com.your.faceunlockapp.activities

import android.app.Activity
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.hardware.face.FaceManager
import android.os.*
import android.util.Log
import android.widget.ImageView
import android.widget.TextView
import java.io.ByteArrayOutputStream
import java.io.FileInputStream
import vendor.milahaina.biometrics.face.IVisionService

class EnrollActivity : Activity() {

    private val TAG = "MiLahainaEnrollActivity"
    private val EXTRA_KEY_CHALLENGE_TOKEN = "hw_auth_token"

    private var mFaceManager: FaceManager? = null
    private var mEnrollmentCancel: CancellationSignal? = null
    private var mToken: ByteArray? = null
    private var mVisionService: IVisionService? = null
    
    // UI Elements
    private var mImageViewPreview: ImageView? = null
    private var mTextViewStatus: TextView? = null

    // Callback implementation sent to the HAL service to receive camera preview frames
    private val mVisionServiceImpl = object : IVisionService.Stub() {
        override fun onFrame(pfd: ParcelFileDescriptor, width: Int, height: Int, angle: Int) {
            try {
                // NV21 frame size is width * height * 1.5
                val size = (width * height * 1.5).toInt()
                val fd = pfd.fileDescriptor
                
                val data = ByteArray(size)
                val fis = FileInputStream(fd)
                fis.read(data)
                
                // Process and render frame on the UI thread
                renderFrame(data, width, height, angle)
                pfd.close()
                
                // Fetch face mesh/landmark coordinates calculated by the engine
                fetchAndUpdateLandmarks()
            } catch (e: Exception) {
                Log.e(TAG, "Error receiving frame from HAL", e)
            }
        }

        override fun setCallback(callback: IVisionService?) {}
        override fun getVendorCode(): Int = 0
        override fun getLastLandmarks(): FloatArray = FloatArray(0)
        override fun getInterfaceVersion() = IVisionService.VERSION
        override fun getInterfaceHash() = IVisionService.HASH
    }

    // Handles standard Android FaceManager callback progression
    private val mEnrollmentCallback = object : FaceManager.EnrollmentCallback() {
        override fun onEnrollmentProgress(remaining: Int) {
            runOnUiThread {
                if (remaining == 0) {
                    mTextViewStatus?.text = "Enrollment Complete!"
                    // Exit enrollment flow
                    setResult(RESULT_OK)
                    finish()
                } else {
                    mTextViewStatus?.text = "Enrolling... Remaining steps: $remaining"
                }
            }
        }

        override fun onEnrollmentHelp(helpMsgId: Int, helpString: CharSequence?) {
            runOnUiThread {
                mTextViewStatus?.text = helpString ?: "Position your face in the circle"
            }
        }

        override fun onEnrollmentError(errMsgId: Int, errString: CharSequence?) {
            runOnUiThread {
                mTextViewStatus?.text = errString ?: "Enrollment failed"
            }
            finish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Extract challenge token passed by Settings app
        mToken = intent.getByteArrayExtra(EXTRA_KEY_CHALLENGE_TOKEN)
        mFaceManager = getSystemService(FaceManager::class.java)

        // Simple layout programmatically:
        val layout = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            gravity = android.view.Gravity.CENTER
        }
        mImageViewPreview = ImageView(this).apply {
            layoutParams = android.widget.LinearLayout.LayoutParams(800, 800)
        }
        mTextViewStatus = TextView(this).apply {
            textSize = 18f
            text = "Initializing face enrollment..."
        }
        layout.addView(mImageViewPreview)
        layout.addView(mTextViewStatus)
        setContentView(layout)

        // Connect to the HAL's IVisionService
        bindVisionService()
    }

    private fun bindVisionService() {
        try {
            Log.i(TAG, "Binding to HAL IVisionService...")
            val binder = ServiceManager.getService("vendor.milahaina.biometrics.face.IVisionService/default")
            if (binder != null) {
                mVisionService = IVisionService.Stub.asInterface(binder)
                // Register callback so the HAL sends camera frames here
                mVisionService?.setCallback(mVisionServiceImpl)
                Log.i(TAG, "Successfully connected and registered callback")
            } else {
                Log.e(TAG, "IVisionService Binder returned null. HAL service not running?")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error connecting to IVisionService", e)
        }
    }

    private fun startEnrollment() {
        if (mToken != null && mToken!!.isNotEmpty()) {
            mEnrollmentCancel = CancellationSignal()
            // Request Android FaceManager to begin enrollment session
            mFaceManager?.enroll(
                0, // userId
                mToken,
                mEnrollmentCancel,
                mEnrollmentCallback,
                intArrayOf(1) // Enrollment feature configurations
            )
            mTextViewStatus?.text = "Looking for face..."
        } else {
            mTextViewStatus?.text = "Error: Invalid enrollment challenge token"
        }
    }

    private fun renderFrame(nv21: ByteArray, width: Int, height: Int, angle: Int) {
        try {
            // Compress raw NV21 to JPEG byte array
            val yuvImage = YuvImage(nv21, ImageFormat.NV21, width, height, null)
            val out = ByteArrayOutputStream()
            yuvImage.compressToJpeg(Rect(0, 0, width, height), 90, out)
            val imageBytes = out.toByteArray()
            val bitmap = BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)

            // Rotate bitmap based on HAL camera orientation angle
            val matrix = android.graphics.Matrix()
            matrix.postRotate(angle.toFloat())
            val rotatedBitmap = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true)

            runOnUiThread {
                mImageViewPreview?.setImageBitmap(rotatedBitmap)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error decoding preview frame", e)
        }
    }

    private fun fetchAndUpdateLandmarks() {
        try {
            val landmarks = mVisionService?.lastLandmarks
            if (landmarks != null && landmarks.isNotEmpty()) {
                // landmarks array contains keypoint coordinates (x, y, z)
                // You can draw these on top of the preview to display a face mesh overlay
                Log.v(TAG, "Received ${landmarks.size / 3} keypoints from face engine")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to get landmarks from IVisionService", e)
        }
    }

    override fun onResume() {
        super.onResume()
        startEnrollment()
    }

    override fun onPause() {
        super.onPause()
        mEnrollmentCancel?.cancel()
    }
}
```
