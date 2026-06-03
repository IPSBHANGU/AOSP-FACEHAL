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

package org.milahaina.vision.view

import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.PointF
import android.graphics.RectF
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import android.view.animation.AccelerateDecelerateInterpolator
import android.view.animation.LinearInterpolator
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin
import kotlin.math.sqrt

/**
 * OnePlus-style face mesh overlay view.
 *
 * Draws animated face landmark points and connecting mesh lines over the
 * circular camera preview. Expects a 13-element float array from the HAL's
 * IVisionService.getLastLandmarks():
 *
 *  [0]       = score          (confidence 0..1)
 *  [1,  2]   = rx, ry         (right eye)
 *  [3,  4]   = lx, ly         (left eye)
 *  [5,  6]   = nx, ny         (nose)
 *  [7,  8]   = mx, my         (mouth center)
 *  [9,  10]  = rex, rey       (right ear — typically 0)
 *  [11, 12]  = lex, ley       (left ear — typically 0)
 *
 * All coordinates are normalised [0.0, 1.0] relative to the upright frame.
 */
class FaceMeshOverlayView : View {

    // ── Public API ──────────────────────────────────────────────────────

    constructor(context: Context?) : super(context) { init() }
    constructor(context: Context?, attrs: AttributeSet?) : super(context, attrs) { init() }
    constructor(context: Context?, attrs: AttributeSet?, defStyleAttr: Int) : super(context, attrs, defStyleAttr) { init() }

    // ── Constants ───────────────────────────────────────────────────────

    companion object {
        /** Minimum confidence score to show the mesh. */
        private const val MIN_SCORE = 0.4f

        /** Interpolation speed — higher = snappier tracking. */
        private const val LERP_FACTOR = 0.35f

        /** Number of landmark points (excluding score). */
        private const val NUM_POINTS = 6  // rEye, lEye, nose, mouth, rEar, lEar

        // Accent colours — OnePlus green palette
        private const val COLOR_PRIMARY   = 0xFF00D084.toInt()  // vibrant green
        private const val COLOR_SECONDARY = 0xFF00E5A0.toInt()  // light green
        private const val COLOR_GLOW      = 0x4000D084.toInt()  // green glow (25% alpha)
        private const val COLOR_LINE      = 0x8800D084.toInt()  // line colour (53% alpha)
        private const val COLOR_RING_BG   = 0x33FFFFFF.toInt()  // ring background
        private const val COLOR_NO_FACE   = 0x66FFFFFF.toInt()  // idle state

        // Dot radii
        private const val DOT_RADIUS_DP       = 4.0f
        private const val DOT_GLOW_RADIUS_DP  = 10.0f
        private const val LINE_WIDTH_DP       = 1.5f

        // Scanning ring
        private const val SCAN_RING_WIDTH_DP  = 2.5f
        private const val SCAN_SWEEP_ANGLE    = 90f
    }

    // ── State ───────────────────────────────────────────────────────────

    /** Current (interpolated) landmark positions in normalised coords. */
    private val mCurrentPoints = Array(NUM_POINTS) { PointF(0.5f, 0.5f) }

    /** Target landmark positions from latest HAL update. */
    private val mTargetPoints = Array(NUM_POINTS) { PointF(0.5f, 0.5f) }

    /** Whether we currently have a valid face detection. */
    private var mHasFace = false

    /** Confidence score from the latest landmark update. */
    private var mScore = 0f

    /** Current enroll progress 0..100. */
    private var mEnrollProgress = 0f

    /** Animated alpha for fade-in/out of the mesh. */
    private var mMeshAlpha = 0f
    private var mFadeAnimator: ValueAnimator? = null

    /** Scanning ring rotation angle (continuous animation). */
    private var mScanAngle = 0f
    private var mScanAnimator: ValueAnimator? = null

    /** Pulse scale for detected dots (continuous animation). */
    private var mPulseScale = 1f
    private var mPulseAnimator: ValueAnimator? = null

    // ── Paints (allocated once) ─────────────────────────────────────────

    private lateinit var mDotPaint: Paint
    private lateinit var mGlowPaint: Paint
    private lateinit var mLinePaint: Paint
    private lateinit var mScanRingPaint: Paint
    private lateinit var mScanRingBgPaint: Paint
    private lateinit var mProgressPaint: Paint

    // ── Density helpers ─────────────────────────────────────────────────

    private var mDensity = 1f
    private val Float.dp get() = this * mDensity

    // ── Mesh topology ───────────────────────────────────────────────────

    /** Index pairs defining the mesh lines between landmark points.
     *  Indices: 0=rEye, 1=lEye, 2=nose, 3=mouth, 4=rEar, 5=lEar */
    private val MESH_EDGES = arrayOf(
        intArrayOf(0, 1),  // right eye → left eye
        intArrayOf(0, 2),  // right eye → nose
        intArrayOf(1, 2),  // left eye → nose
        intArrayOf(2, 3),  // nose → mouth
        intArrayOf(0, 4),  // right eye → right ear (only if ear != 0)
        intArrayOf(1, 5),  // left eye → left ear (only if ear != 0)
    )

    // ── Initialisation ──────────────────────────────────────────────────

    private fun init() {
        mDensity = resources.displayMetrics.density

        mDotPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.FILL
            color = COLOR_PRIMARY
        }

        mGlowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.FILL
            color = COLOR_GLOW
        }

        mLinePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            color = COLOR_LINE
            strokeWidth = LINE_WIDTH_DP.dp
            strokeCap = Paint.Cap.ROUND
        }

        mScanRingPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = SCAN_RING_WIDTH_DP.dp
            strokeCap = Paint.Cap.ROUND
        }

        mScanRingBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = SCAN_RING_WIDTH_DP.dp
            color = COLOR_RING_BG
        }

        mProgressPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            style = Paint.Style.STROKE
            strokeWidth = SCAN_RING_WIDTH_DP.dp
            strokeCap = Paint.Cap.ROUND
        }

        // Start the continuous scan-ring rotation
        startScanAnimation()
        startPulseAnimation()
    }

    // ── Public methods ──────────────────────────────────────────────────

    /**
     * Update the landmark positions from the HAL.
     * @param landmarks 13-element float array, or null if no face detected.
     */
    fun updateLandmarks(landmarks: FloatArray?) {
        if (landmarks != null && landmarks.size >= 13 && landmarks[0] >= MIN_SCORE) {
            mScore = landmarks[0]
            // Map from the flat 13-element array into our 6 PointF targets
            mTargetPoints[0].set(landmarks[1], landmarks[2])   // right eye
            mTargetPoints[1].set(landmarks[3], landmarks[4])   // left eye
            mTargetPoints[2].set(landmarks[5], landmarks[6])   // nose
            mTargetPoints[3].set(landmarks[7], landmarks[8])   // mouth
            mTargetPoints[4].set(landmarks[9], landmarks[10])  // right ear
            mTargetPoints[5].set(landmarks[11], landmarks[12]) // left ear

            if (!mHasFace) {
                mHasFace = true
                // Snap to target on first detection (no interpolation lag)
                for (i in mCurrentPoints.indices) {
                    mCurrentPoints[i].set(mTargetPoints[i].x, mTargetPoints[i].y)
                }
                animateFade(targetAlpha = 1f)
            }
        } else {
            if (mHasFace) {
                mHasFace = false
                mScore = 0f
                animateFade(targetAlpha = 0f)
            }
        }

        // Smooth interpolation towards targets
        for (i in mCurrentPoints.indices) {
            mCurrentPoints[i].x = lerp(mCurrentPoints[i].x, mTargetPoints[i].x, LERP_FACTOR)
            mCurrentPoints[i].y = lerp(mCurrentPoints[i].y, mTargetPoints[i].y, LERP_FACTOR)
        }

        postInvalidate()
    }

    /**
     * Update the current enrollment progress.
     * @param progress 0..100
     */
    fun setEnrollProgress(progress: Float) {
        mEnrollProgress = progress.coerceIn(0f, 100f)
        postInvalidate()
    }

    // ── Drawing ─────────────────────────────────────────────────────────

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)

        val cx = measuredWidth / 2f
        val cy = measuredHeight / 2f
        val radius = min(cx, cy)

        // Clip to the same circle as CircleSurfaceView (90% inset)
        val clipPath = Path()
        clipPath.addCircle(cx, cy, radius * 0.90f, Path.Direction.CCW)
        canvas.save()
        canvas.clipPath(clipPath)

        // ── Scanning ring (visible when no face or low progress) ────────
        drawScanRing(canvas, cx, cy, radius)

        // ── Mesh overlay (face detected) ────────────────────────────────
        if (mMeshAlpha > 0.01f) {
            val alpha = (mMeshAlpha * 255).toInt()
            drawMeshLines(canvas, cx, cy, radius, alpha)
            drawLandmarkDots(canvas, cx, cy, radius, alpha)
        }

        canvas.restore()
    }

    private fun drawScanRing(canvas: Canvas, cx: Float, cy: Float, radius: Float) {
        val ringRadius = radius * 0.85f
        val ringRect = RectF(
            cx - ringRadius, cy - ringRadius,
            cx + ringRadius, cy + ringRadius
        )

        // Background ring (subtle)
        mScanRingBgPaint.alpha = if (mHasFace) 60 else 40
        canvas.drawArc(ringRect, 0f, 360f, false, mScanRingBgPaint)

        if (mEnrollProgress > 0f && mEnrollProgress < 100f) {
            // Progress arc — draw with gradient
            val sweepAngle = mEnrollProgress * 3.6f
            mProgressPaint.shader = LinearGradient(
                cx - ringRadius, cy, cx + ringRadius, cy,
                COLOR_PRIMARY, COLOR_SECONDARY, Shader.TileMode.CLAMP
            )
            canvas.drawArc(ringRect, 270f, sweepAngle, false, mProgressPaint)
        } else if (!mHasFace || mEnrollProgress < 1f) {
            // Scanning sweep — animated rotation when idle/no face
            mScanRingPaint.shader = null
            mScanRingPaint.color = if (mHasFace) COLOR_PRIMARY else COLOR_NO_FACE

            // Draw a gradient sweep arc
            val startAngle = mScanAngle
            mScanRingPaint.alpha = 180
            canvas.drawArc(ringRect, startAngle, SCAN_SWEEP_ANGLE, false, mScanRingPaint)

            // Trailing fade
            mScanRingPaint.alpha = 60
            canvas.drawArc(ringRect, startAngle - 30f, 30f, false, mScanRingPaint)
        } else if (mEnrollProgress >= 100f) {
            // Complete — full green ring
            mProgressPaint.shader = null
            mProgressPaint.color = COLOR_PRIMARY
            canvas.drawArc(ringRect, 0f, 360f, false, mProgressPaint)
        }
    }

    private fun drawMeshLines(canvas: Canvas, cx: Float, cy: Float, radius: Float, alpha: Int) {
        val viewSize = radius * 2f
        mLinePaint.alpha = (alpha * 0.6f).toInt()

        for (edge in MESH_EDGES) {
            val p1 = mCurrentPoints[edge[0]]
            val p2 = mCurrentPoints[edge[1]]

            // Skip ear edges if ear coordinates are at origin
            if ((edge[0] == 4 || edge[0] == 5) && p1.x == 0f && p1.y == 0f) continue
            if ((edge[1] == 4 || edge[1] == 5) && p2.x == 0f && p2.y == 0f) continue

            val x1 = cx - radius + p1.x * viewSize
            val y1 = cy - radius + p1.y * viewSize
            val x2 = cx - radius + p2.x * viewSize
            val y2 = cy - radius + p2.y * viewSize

            canvas.drawLine(x1, y1, x2, y2, mLinePaint)
        }

        // Additional mesh lines — face contour approximation
        // eye → ear → mouth (creates a face outline feel)
        val rEye = mCurrentPoints[0]
        val lEye = mCurrentPoints[1]
        val mouth = mCurrentPoints[3]

        // Synthesise cheek-bone points from eye/mouth midpoints for a richer mesh
        val rCheekX = cx - radius + ((rEye.x * 0.7f + mouth.x * 0.3f) * viewSize)
        val rCheekY = cy - radius + ((rEye.y * 0.3f + mouth.y * 0.7f) * viewSize)
        val lCheekX = cx - radius + ((lEye.x * 0.7f + mouth.x * 0.3f) * viewSize)
        val lCheekY = cy - radius + ((lEye.y * 0.3f + mouth.y * 0.7f) * viewSize)

        val rEyeX = cx - radius + rEye.x * viewSize
        val rEyeY = cy - radius + rEye.y * viewSize
        val lEyeX = cx - radius + lEye.x * viewSize
        val lEyeY = cy - radius + lEye.y * viewSize
        val mouthX = cx - radius + mouth.x * viewSize
        val mouthY = cy - radius + mouth.y * viewSize

        mLinePaint.alpha = (alpha * 0.35f).toInt()
        canvas.drawLine(rEyeX, rEyeY, rCheekX, rCheekY, mLinePaint)
        canvas.drawLine(lEyeX, lEyeY, lCheekX, lCheekY, mLinePaint)
        canvas.drawLine(rCheekX, rCheekY, mouthX, mouthY, mLinePaint)
        canvas.drawLine(lCheekX, lCheekY, mouthX, mouthY, mLinePaint)

        // Forehead line
        val foreheadY = cy - radius + ((rEye.y + lEye.y) / 2f - 0.08f) * viewSize
        val foreheadLX = cx - radius + (rEye.x - 0.02f) * viewSize
        val foreheadRX = cx - radius + (lEye.x + 0.02f) * viewSize
        canvas.drawLine(foreheadLX, foreheadY, rEyeX, rEyeY, mLinePaint)
        canvas.drawLine(foreheadRX, foreheadY, lEyeX, lEyeY, mLinePaint)
        canvas.drawLine(foreheadLX, foreheadY, foreheadRX, foreheadY, mLinePaint)

        // Chin line
        val chinY = cy - radius + (mouth.y + 0.06f) * viewSize
        mLinePaint.alpha = (alpha * 0.25f).toInt()
        canvas.drawLine(rCheekX, rCheekY, cx, chinY, mLinePaint)
        canvas.drawLine(lCheekX, lCheekY, cx, chinY, mLinePaint)
        canvas.drawLine(mouthX, mouthY, cx, chinY, mLinePaint)
    }

    private fun drawLandmarkDots(canvas: Canvas, cx: Float, cy: Float, radius: Float, alpha: Int) {
        val viewSize = radius * 2f
        val dotRadius = DOT_RADIUS_DP.dp
        val glowRadius = DOT_GLOW_RADIUS_DP.dp * mPulseScale

        for (i in 0 until NUM_POINTS) {
            val pt = mCurrentPoints[i]

            // Skip ear points at origin
            if ((i == 4 || i == 5) && pt.x == 0f && pt.y == 0f) continue

            val px = cx - radius + pt.x * viewSize
            val py = cy - radius + pt.y * viewSize

            // Glow halo
            mGlowPaint.alpha = (alpha * 0.4f * mPulseScale).toInt()
            canvas.drawCircle(px, py, glowRadius, mGlowPaint)

            // Solid dot
            mDotPaint.alpha = alpha
            // Eye points slightly larger
            val r = if (i <= 1) dotRadius * 1.2f else dotRadius
            canvas.drawCircle(px, py, r, mDotPaint)
        }

        // Draw the synthesised cheek-bone points as smaller dots
        val mouth = mCurrentPoints[3]
        val rEye = mCurrentPoints[0]
        val lEye = mCurrentPoints[1]

        val smallDot = dotRadius * 0.6f
        mDotPaint.alpha = (alpha * 0.5f).toInt()

        val rCheekX = cx - radius + ((rEye.x * 0.7f + mouth.x * 0.3f) * viewSize)
        val rCheekY = cy - radius + ((rEye.y * 0.3f + mouth.y * 0.7f) * viewSize)
        canvas.drawCircle(rCheekX, rCheekY, smallDot, mDotPaint)

        val lCheekX = cx - radius + ((lEye.x * 0.7f + mouth.x * 0.3f) * viewSize)
        val lCheekY = cy - radius + ((lEye.y * 0.3f + mouth.y * 0.7f) * viewSize)
        canvas.drawCircle(lCheekX, lCheekY, smallDot, mDotPaint)

        // Forehead point
        val foreheadX = cx
        val foreheadY = cy - radius + ((rEye.y + lEye.y) / 2f - 0.08f) * viewSize
        canvas.drawCircle(foreheadX, foreheadY, smallDot, mDotPaint)

        // Chin point
        val chinX = cx
        val chinY = cy - radius + (mouth.y + 0.06f) * viewSize
        canvas.drawCircle(chinX, chinY, smallDot, mDotPaint)
    }

    // ── Animations ──────────────────────────────────────────────────────

    private fun startScanAnimation() {
        mScanAnimator?.cancel()
        mScanAnimator = ValueAnimator.ofFloat(0f, 360f).apply {
            duration = 2000
            repeatCount = ValueAnimator.INFINITE
            interpolator = LinearInterpolator()
            addUpdateListener {
                mScanAngle = it.animatedValue as Float
                postInvalidate()
            }
            start()
        }
    }

    private fun startPulseAnimation() {
        mPulseAnimator?.cancel()
        mPulseAnimator = ValueAnimator.ofFloat(0.85f, 1.15f).apply {
            duration = 1200
            repeatCount = ValueAnimator.INFINITE
            repeatMode = ValueAnimator.REVERSE
            interpolator = AccelerateDecelerateInterpolator()
            addUpdateListener {
                mPulseScale = it.animatedValue as Float
                // Only invalidate if mesh is visible
                if (mMeshAlpha > 0.01f) {
                    postInvalidate()
                }
            }
            start()
        }
    }

    private fun animateFade(targetAlpha: Float) {
        mFadeAnimator?.cancel()
        mFadeAnimator = ValueAnimator.ofFloat(mMeshAlpha, targetAlpha).apply {
            duration = 250
            interpolator = AccelerateDecelerateInterpolator()
            addUpdateListener {
                mMeshAlpha = it.animatedValue as Float
                postInvalidate()
            }
            start()
        }
    }

    // ── Helpers ──────────────────────────────────────────────────────────

    private fun lerp(start: Float, end: Float, fraction: Float): Float {
        return start + (end - start) * fraction
    }

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        mScanAnimator?.cancel()
        mPulseAnimator?.cancel()
        mFadeAnimator?.cancel()
    }
}
