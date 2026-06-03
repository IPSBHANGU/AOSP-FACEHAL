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

package org.milahaina.vision.receiver

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.ServiceManager
import android.util.Log
import androidx.core.app.NotificationCompat
import org.milahaina.vision.R
import org.milahaina.vision.util.Constants
import vendor.milahaina.biometrics.face.IVisionService

class ReEnrollmentReceiver : BroadcastReceiver() {
    companion object {
        private const val TAG = "ReEnrollmentReceiver"
        private const val CHANNEL_ID = "FaceEnrollmentChannel"
    }

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_USER_PRESENT) return

        try {
            val binder = ServiceManager.getService("vendor.milahaina.biometrics.face.IVisionService/default")
            if (binder == null) {
                Log.w(TAG, "IVisionService not available")
                return
            }

            val visionService = IVisionService.Stub.asInterface(binder)
            val code = visionService.vendorCode
            
            if (code == Constants.MSG_REENROLLMENT_REQUIRED) {
                Log.i(TAG, "Received REQUIRE_REENROLLMENT code. Showing notification.")
                showNotification(context)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error checking vendor code", e)
        }
    }

    private fun showNotification(context: Context) {
        val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        val channel = NotificationChannel(
            CHANNEL_ID,
            "Face Unlock Notifications",
            NotificationManager.IMPORTANCE_HIGH
        )
        notificationManager.createNotificationChannel(channel)

        val enrollIntent = Intent(android.provider.Settings.ACTION_BIOMETRIC_ENROLL).apply {
            putExtra(android.provider.Settings.EXTRA_BIOMETRIC_AUTHENTICATORS_ALLOWED, 
                     android.hardware.biometrics.BiometricManager.Authenticators.BIOMETRIC_STRONG)
            flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
        }
        val pendingIntent = PendingIntent.getActivity(
            context, 0, enrollIntent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val notification = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_face_header)
            .setContentTitle(context.getString(R.string.app_name))
            .setContentText(context.getString(R.string.unlock_reenroll_required))
            .setPriority(NotificationCompat.PRIORITY_HIGH)
            .setAutoCancel(true)
            .setContentIntent(pendingIntent)
            .build()

        notificationManager.notify(1059, notification)
    }
}
