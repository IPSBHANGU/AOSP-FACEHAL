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

package org.milahaina.vision.activities

import android.content.Intent
import android.content.res.Resources
import android.hardware.face.FaceManager
import android.media.AudioAttributes
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.os.*
import android.os.ServiceManager
import android.util.Log
import android.widget.TextView
import org.milahaina.vision.R
import org.milahaina.vision.util.Constants
import org.milahaina.vision.util.PreferenceHelper
import org.milahaina.vision.util.Util
import org.milahaina.vision.util.VendorCodeMessages
import org.milahaina.vision.view.CircleSurfaceView
import com.google.android.setupdesign.GlifLayout
import com.google.android.setupdesign.util.ThemeHelper

import vendor.milahaina.biometrics.face.IVisionService


open class EnrollActivity : FaceBaseActivity() {
    private var mEnrollmentCancel: CancellationSignal? = null
    private var mFaceManager: FaceManager? = null
    private var mPreferenceHelper: PreferenceHelper? = null
    private var mSurfaceView: CircleSurfaceView? = null
    private var mEnrollVendorMessage: TextView? = null
    private var mProgress = 0.0f
    private var mFinishScheduled = false
    private var mIsActivityPaused = false
    private var mVisionService: IVisionService? = null

    private val mVisionServiceImpl = object : vendor.milahaina.biometrics.face.IVisionService.Stub() {
        override fun onFrame(pfd: ParcelFileDescriptor, width: Int, height: Int, angle: Int) {
            try {
                val size = width * height * 3 / 2   // NV21 = Y + (W*H/2) chroma
                val data = ByteArray(size)
                ParcelFileDescriptor.AutoCloseInputStream(pfd).use { autoFis ->
                    java.io.DataInputStream(autoFis).readFully(data)
                }
                renderFrame(data, width, height, angle)
            } catch (e: Exception) {
                Log.e(TAG, "Error receiving frame from HAL", e)
            }
        }

        override fun setCallback(callback: IVisionService?) {
            // Callback-to-App direction: not used
        }

        override fun getVendorCode(): Int = 0
        override fun getLastLandmarks(): FloatArray = FloatArray(0)
        override fun getInterfaceVersion(): Int = IVisionService.VERSION
        override fun getInterfaceHash(): String = IVisionService.HASH
    }

    private fun renderFrame(nv21: ByteArray, width: Int, height: Int, angle: Int) {
        try {
            val yuvImage = YuvImage(nv21, ImageFormat.NV21, width, height, null)
            val out = java.io.ByteArrayOutputStream()
            yuvImage.compressToJpeg(Rect(0, 0, width, height), 90, out)
            val imageBytes = out.toByteArray()
            val bitmap = BitmapFactory.decodeByteArray(imageBytes, 0, imageBytes.size)

            // Rotate bitmap if needed based on angle
            val matrix = android.graphics.Matrix()
            matrix.postRotate(angle.toFloat())
            matrix.postScale(-1f, 1f)
            val rotatedBitmap = Bitmap.createBitmap(bitmap, 0, 0, bitmap.width, bitmap.height, matrix, true)

            runOnUiThread {
                mSurfaceView?.setFrame(rotatedBitmap)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error rendering frame", e)
        }
    }

    private val mEnrollmentCallback: FaceManager.EnrollmentCallback =
        object : FaceManager.EnrollmentCallback() {
            override fun onEnrollmentProgress(remaining: Int) {
                runOnUiThread {
                    val nextProgress = if (remaining == 0) {
                        100.0f
                    } else {
                        ((ENROLL_REQUIRED_GOOD_FRAMES - remaining)
                            .coerceIn(0, ENROLL_REQUIRED_GOOD_FRAMES) * 100.0f) /
                            ENROLL_REQUIRED_GOOD_FRAMES
                    }

                    if (nextProgress > mProgress) {
                        mProgress = nextProgress
                        mSurfaceView?.setProgress(mProgress)
                    }
                    mEnrollVendorMessage?.text = ""

                    if (remaining == 0 && !mFinishScheduled) {
                        mFinishScheduled = true
                        Handler(Looper.getMainLooper()).postDelayed({
                            val enrollActivity = this@EnrollActivity
                            if (!enrollActivity.isDestroyed) {
                                startFinishActivity()
                            }
                        }, 2000)
                    }
                }
            }

            override fun onEnrollmentHelp(helpMessageId: Int, charSequence: CharSequence?) {
                val qualityCode = VendorCodeMessages.resolveFaceQualityVendorCode(
                    helpMessageId, charSequence
                )
                val stringRes = qualityCode?.let {
                    VendorCodeMessages.stringResForFaceQualityVendor(it)
                }
                runOnUiThread {
                    if (stringRes != null) {
                        mEnrollVendorMessage?.setText(stringRes)
                    } else {
                        // KEEP / unknown vendor codes shouldn't pollute the hint.
                        mEnrollVendorMessage?.text = ""
                    }
                }
            }

            override fun onEnrollmentError(errorMessageId: Int, charSequence: CharSequence?) {
                val cameraVendorCode = VendorCodeMessages.resolveCameraVendorCode(errorMessageId, charSequence)
                if (cameraVendorCode == null) {
                    runOnUiThread {
                        // Keep enrollment UI stable for non-camera vendor failures reported by HAL.
                        mEnrollVendorMessage?.text = ""
                    }
                    Log.i(TAG, "Ignoring non-camera enrollment error in UI: frameworkId=$errorMessageId")
                    return
                }

                if (!mIsActivityPaused) {
                    val intent = Intent()
                    intent.setClass(this@EnrollActivity, TryAgainActivity::class.java)
                    intent.putExtra(Constants.EXTRA_KEY_ENROLL_CAMERA_VENDOR_CODE, cameraVendorCode)
                    Log.i(TAG, "Enrollment camera-related error: halVendorCode=$cameraVendorCode frameworkId=$errorMessageId")
                    if (errorMessageId != Constants.MSG_UNLOCK_FAILED) {
                        parseIntent(intent)
                    } else {
                        setResult(-1)
                    }
                    startActivity(intent)
                }
                finish()
            }
        }

    override fun onCreate(bundle: Bundle?) {
        ThemeHelper.applyTheme(this)
        ThemeHelper.trySetDynamicColor(this)
        super.onCreate(bundle)
        mPreferenceHelper = PreferenceHelper(this)
        mFaceManager = getSystemService(FaceManager::class.java)
        setContentView(R.layout.face_enroll)
        setHeaderText(R.string.face_enroll_title)
        getLayout().setDescriptionText(R.string.face_enroll_description)
        mSurfaceView = findViewById(R.id.camera_surface)
        mEnrollVendorMessage = findViewById(R.id.face_vendor_message)

        // Bind to the HAL's IVisionService once
        bindVisionService()
    }

    private fun bindVisionService() {
        try {
            Log.i(TAG, "Searching for HAL IVisionService...")
            val binder = ServiceManager.getService("vendor.milahaina.biometrics.face.IVisionService/default")
            if (binder != null) {
                mVisionService = IVisionService.Stub.asInterface(binder)
                // Pass our local implementation as the callback to the HAL
                mVisionService?.setCallback(mVisionServiceImpl)
                Log.i(TAG, "Connected to HAL IVisionService and registered callback")
            } else {
                Log.e(TAG, "HAL IVisionService not found")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error interacting with IVisionService", e)
        }
    }

    override fun onApplyThemeResource(theme: Resources.Theme, resid: Int, first: Boolean) {
        theme.applyStyle(R.style.SetupWizardPartnerResource, true)
        super.onApplyThemeResource(theme, resid, first)
    }

    private fun startEnrollment() {
        mProgress = 0.0f
        mFinishScheduled = false
        mSurfaceView?.setProgress(0.0f)

        if (mToken != null && mToken!!.isNotEmpty()) {
            mEnrollmentCancel = CancellationSignal()
            mFaceManager!!.enroll(
                Util.getUserId(this),
                mToken,
                mEnrollmentCancel,
                mEnrollmentCallback,
                intArrayOf(1)
            )
        }
    }

    override fun getLayout(): GlifLayout {
        return findViewById(R.id.face_enroll)!!
    }

    override fun onDestroy() {
        super.onDestroy()
        Log.i(TAG, "onDestroy")
    }

    override fun onPause() {
        super.onPause()
        mIsActivityPaused = true
        mEnrollmentCancel?.cancel()
    }

    override fun onResume() {
        super.onResume()
        mIsActivityPaused = false
        startEnrollment()
    }

    private fun startFinishActivity() {
        val intent = Intent()
        intent.setClass(this@EnrollActivity, FaceEnrollFinishActivity::class.java)
        if (mToken != null) {
            intent.putExtra(Constants.EXTRA_KEY_CHALLENGE_TOKEN, mToken)
        }
        if (mUserId != UserHandle.USER_NULL) {
            intent.putExtra(Intent.EXTRA_USER_ID, mUserId)
        }
        startActivityForResult(intent, REQUEST_FINISH)
    }

    override fun onBackPressed() {
        super.onBackPressed()
        finish()
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_FINISH) {
            setResult(resultCode, data)
            finish()
        }
    }

    companion object {
        private const val REQUEST_FINISH = 1
        private val TAG = EnrollActivity::class.java.simpleName
        private val CLICK_VIBRATION: VibrationEffect = VibrationEffect.get(0)
        private val SONIFICATION = AudioAttributes.Builder()
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION).build()

        private const val ENROLL_REQUIRED_GOOD_FRAMES = 5
    }
}
