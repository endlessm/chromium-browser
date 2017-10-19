/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

package org.webrtc;

import android.annotation.TargetApi;
import android.graphics.Matrix;
import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.opengl.GLES20;
import android.os.Bundle;
import android.view.Surface;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.Deque;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.LinkedBlockingDeque;

/** Android hardware video encoder. */
@TargetApi(19)
@SuppressWarnings("deprecation") // Cannot support API level 19 without using deprecated methods.
class HardwareVideoEncoder implements VideoEncoder {
  private static final String TAG = "HardwareVideoEncoder";

  // Bitrate modes - should be in sync with OMX_VIDEO_CONTROLRATETYPE defined
  // in OMX_Video.h
  private static final int VIDEO_ControlRateConstant = 2;
  // Key associated with the bitrate control mode value (above). Not present as a MediaFormat
  // constant until API level 21.
  private static final String KEY_BITRATE_MODE = "bitrate-mode";

  private static final int MAX_VIDEO_FRAMERATE = 30;

  // See MAX_ENCODER_Q_SIZE in androidmediaencoder_jni.cc.
  private static final int MAX_ENCODER_Q_SIZE = 2;

  private static final int MEDIA_CODEC_RELEASE_TIMEOUT_MS = 5000;
  private static final int DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US = 100000;

  private final String codecName;
  private final VideoCodecType codecType;
  private final int colorFormat;
  private final ColorFormat inputColorFormat;
  // Base interval for generating key frames.
  private final int keyFrameIntervalSec;
  // Interval at which to force a key frame. Used to reduce color distortions caused by some
  // Qualcomm video encoders.
  private final long forcedKeyFrameMs;
  // Presentation timestamp of the last requested (or forced) key frame.
  private long lastKeyFrameMs;

  private final BitrateAdjuster bitrateAdjuster;
  private int adjustedBitrate;

  // A queue of EncodedImage.Builders that correspond to frames in the codec.  These builders are
  // pre-populated with all the information that can't be sent through MediaCodec.
  private final Deque<EncodedImage.Builder> outputBuilders;

  // Thread that delivers encoded frames to the user callback.
  private Thread outputThread;

  // Whether the encoder is running.  Volatile so that the output thread can watch this value and
  // exit when the encoder stops.
  private volatile boolean running = false;
  // Any exception thrown during shutdown.  The output thread releases the MediaCodec and uses this
  // value to send exceptions thrown during release back to the encoder thread.
  private volatile Exception shutdownException = null;

  // Surface objects for texture-mode encoding.

  // EGL context shared with the application.  Used to access texture inputs.
  private EglBase14.Context textureContext;
  // EGL base wrapping the shared texture context.  Holds hooks to both the shared context and the
  // input surface.  Making this base current allows textures from the context to be drawn onto the
  // surface.
  private EglBase14 textureEglBase;
  // Input surface for the codec.  The encoder will draw input textures onto this surface.
  private Surface textureInputSurface;
  // Drawer used to draw input textures onto the codec's input surface.
  private GlRectDrawer textureDrawer;

  private MediaCodec codec;
  private Callback callback;

  private int width;
  private int height;

  // Contents of the last observed config frame output by the MediaCodec. Used by H.264.
  private ByteBuffer configBuffer = null;

  /**
   * Creates a new HardwareVideoEncoder with the given codecName, codecType, colorFormat, key frame
   * intervals, and bitrateAdjuster.
   *
   * @param codecName the hardware codec implementation to use
   * @param codecType the type of the given video codec (eg. VP8, VP9, or H264)
   * @param colorFormat color format used by the input buffer
   * @param keyFrameIntervalSec interval in seconds between key frames; used to initialize the codec
   * @param forceKeyFrameIntervalMs interval at which to force a key frame if one is not requested;
   *     used to reduce distortion caused by some codec implementations
   * @param bitrateAdjuster algorithm used to correct codec implementations that do not produce the
   *     desired bitrates
   * @throws IllegalArgumentException if colorFormat is unsupported
   */
  public HardwareVideoEncoder(String codecName, VideoCodecType codecType, int colorFormat,
      int keyFrameIntervalSec, int forceKeyFrameIntervalMs, BitrateAdjuster bitrateAdjuster,
      EglBase14.Context textureContext) {
    this.codecName = codecName;
    this.codecType = codecType;
    this.colorFormat = colorFormat;
    if (textureContext == null) {
      this.inputColorFormat = ColorFormat.valueOf(colorFormat);
    } else {
      // ColorFormat copies bytes between buffers.  It is not used in texture mode.
      this.inputColorFormat = null;
    }
    this.keyFrameIntervalSec = keyFrameIntervalSec;
    this.forcedKeyFrameMs = forceKeyFrameIntervalMs;
    this.bitrateAdjuster = bitrateAdjuster;
    this.outputBuilders = new LinkedBlockingDeque<>();
    this.textureContext = textureContext;
  }

  @Override
  public VideoCodecStatus initEncode(Settings settings, Callback callback) {
    return initEncodeInternal(
        settings.width, settings.height, settings.startBitrate, settings.maxFramerate, callback);
  }

  private VideoCodecStatus initEncodeInternal(
      int width, int height, int bitrateKbps, int fps, Callback callback) {
    Logging.d(
        TAG, "initEncode: " + width + " x " + height + ". @ " + bitrateKbps + "kbps. Fps: " + fps);
    this.width = width;
    this.height = height;
    if (bitrateKbps != 0 && fps != 0) {
      bitrateAdjuster.setTargets(bitrateKbps * 1000, fps);
    }
    adjustedBitrate = bitrateAdjuster.getAdjustedBitrateBps();

    this.callback = callback;

    lastKeyFrameMs = -1;

    try {
      codec = MediaCodec.createByCodecName(codecName);
    } catch (IOException | IllegalArgumentException e) {
      Logging.e(TAG, "Cannot create media encoder " + codecName);
      return VideoCodecStatus.ERROR;
    }
    try {
      MediaFormat format = MediaFormat.createVideoFormat(codecType.mimeType(), width, height);
      format.setInteger(MediaFormat.KEY_BIT_RATE, adjustedBitrate);
      format.setInteger(KEY_BITRATE_MODE, VIDEO_ControlRateConstant);
      format.setInteger(MediaFormat.KEY_COLOR_FORMAT, colorFormat);
      format.setInteger(MediaFormat.KEY_FRAME_RATE, bitrateAdjuster.getAdjustedFramerate());
      format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, keyFrameIntervalSec);
      Logging.d(TAG, "Format: " + format);
      codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);

      if (textureContext != null) {
        // Texture mode.
        textureEglBase = new EglBase14(textureContext, EglBase.CONFIG_RECORDABLE);
        textureInputSurface = codec.createInputSurface();
        textureEglBase.createSurface(textureInputSurface);
        textureDrawer = new GlRectDrawer();
      }

      codec.start();
    } catch (IllegalStateException e) {
      Logging.e(TAG, "initEncode failed", e);
      release();
      return VideoCodecStatus.ERROR;
    }

    running = true;
    outputThread = createOutputThread();
    outputThread.start();

    return VideoCodecStatus.OK;
  }

  @Override
  public VideoCodecStatus release() {
    try {
      if (outputThread == null) {
        return VideoCodecStatus.OK;
      }
      // The outputThread actually stops and releases the codec once running is false.
      running = false;
      if (!ThreadUtils.joinUninterruptibly(outputThread, MEDIA_CODEC_RELEASE_TIMEOUT_MS)) {
        Logging.e(TAG, "Media encoder release timeout");
        return VideoCodecStatus.TIMEOUT;
      }
      if (shutdownException != null) {
        // Log the exception and turn it into an error.
        Logging.e(TAG, "Media encoder release exception", shutdownException);
        return VideoCodecStatus.ERROR;
      }
    } finally {
      codec = null;
      outputThread = null;
      outputBuilders.clear();

      if (textureDrawer != null) {
        textureDrawer.release();
        textureDrawer = null;
      }
      if (textureEglBase != null) {
        textureEglBase.release();
        textureEglBase = null;
      }
      if (textureInputSurface != null) {
        textureInputSurface.release();
        textureInputSurface = null;
      }
    }
    return VideoCodecStatus.OK;
  }

  @Override
  public VideoCodecStatus encode(VideoFrame videoFrame, EncodeInfo encodeInfo) {
    if (codec == null) {
      return VideoCodecStatus.UNINITIALIZED;
    }

    // If input resolution changed, restart the codec with the new resolution.
    int frameWidth = videoFrame.getBuffer().getWidth();
    int frameHeight = videoFrame.getBuffer().getHeight();
    if (frameWidth != width || frameHeight != height) {
      VideoCodecStatus status = resetCodec(frameWidth, frameHeight);
      if (status != VideoCodecStatus.OK) {
        return status;
      }
    }

    if (outputBuilders.size() > MAX_ENCODER_Q_SIZE) {
      // Too many frames in the encoder.  Drop this frame.
      Logging.e(TAG, "Dropped frame, encoder queue full");
      return VideoCodecStatus.OK; // See webrtc bug 2887.
    }

    boolean requestedKeyFrame = false;
    for (EncodedImage.FrameType frameType : encodeInfo.frameTypes) {
      if (frameType == EncodedImage.FrameType.VideoFrameKey) {
        requestedKeyFrame = true;
      }
    }

    // Frame timestamp rounded to the nearest microsecond and millisecond.
    long presentationTimestampUs = (videoFrame.getTimestampNs() + 500) / 1000;
    long presentationTimestampMs = (presentationTimestampUs + 500) / 1000;
    if (requestedKeyFrame || shouldForceKeyFrame(presentationTimestampMs)) {
      requestKeyFrame(presentationTimestampMs);
    }

    VideoFrame.Buffer videoFrameBuffer = videoFrame.getBuffer();
    // Number of bytes in the video buffer. Y channel is sampled at one byte per pixel; U and V are
    // subsampled at one byte per four pixels.
    int bufferSize = videoFrameBuffer.getHeight() * videoFrameBuffer.getWidth() * 3 / 2;
    EncodedImage.Builder builder = EncodedImage.builder()
                                       .setCaptureTimeMs(presentationTimestampMs)
                                       .setCompleteFrame(true)
                                       .setEncodedWidth(videoFrame.getBuffer().getWidth())
                                       .setEncodedHeight(videoFrame.getBuffer().getHeight())
                                       .setRotation(videoFrame.getRotation());
    outputBuilders.offer(builder);

    if (textureContext != null) {
      if (!(videoFrameBuffer instanceof VideoFrame.TextureBuffer)) {
        Logging.e(TAG, "Cannot encode non-texture buffer in texture mode");
        return VideoCodecStatus.ERROR;
      }
      VideoFrame.TextureBuffer textureBuffer = (VideoFrame.TextureBuffer) videoFrameBuffer;
      return encodeTextureBuffer(videoFrame, textureBuffer);
    } else {
      if (videoFrameBuffer instanceof VideoFrame.TextureBuffer) {
        Logging.w(TAG, "Encoding texture buffer in byte mode; this may be inefficient");
      }
      return encodeByteBuffer(videoFrame, videoFrameBuffer, bufferSize, presentationTimestampUs);
    }
  }

  private VideoCodecStatus encodeTextureBuffer(
      VideoFrame videoFrame, VideoFrame.TextureBuffer textureBuffer) {
    Matrix matrix = textureBuffer.getTransformMatrix();
    float[] transformationMatrix = RendererCommon.convertMatrixFromAndroidGraphicsMatrix(matrix);

    try {
      textureEglBase.makeCurrent();
      // TODO(perkj): glClear() shouldn't be necessary since every pixel is covered anyway,
      // but it's a workaround for bug webrtc:5147.
      GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
      switch (textureBuffer.getType()) {
        case OES:
          textureDrawer.drawOes(textureBuffer.getTextureId(), transformationMatrix, width, height,
              0, 0, width, height);
          break;
        case RGB:
          textureDrawer.drawRgb(textureBuffer.getTextureId(), transformationMatrix, width, height,
              0, 0, width, height);
          break;
      }
      textureEglBase.swapBuffers(videoFrame.getTimestampNs());
    } catch (RuntimeException e) {
      Logging.e(TAG, "encodeTexture failed", e);
      // Keep the output builders in sync with buffers in the codec.
      outputBuilders.pollLast();
      return VideoCodecStatus.ERROR;
    }
    return VideoCodecStatus.OK;
  }

  private VideoCodecStatus encodeByteBuffer(VideoFrame videoFrame,
      VideoFrame.Buffer videoFrameBuffer, int bufferSize, long presentationTimestampUs) {
    // No timeout.  Don't block for an input buffer, drop frames if the encoder falls behind.
    int index;
    try {
      index = codec.dequeueInputBuffer(0 /* timeout */);
    } catch (IllegalStateException e) {
      Logging.e(TAG, "dequeueInputBuffer failed", e);
      return VideoCodecStatus.FALLBACK_SOFTWARE;
    }

    if (index == -1) {
      // Encoder is falling behind.  No input buffers available.  Drop the frame.
      Logging.e(TAG, "Dropped frame, no input buffers available");
      return VideoCodecStatus.OK; // See webrtc bug 2887.
    }

    ByteBuffer buffer;
    try {
      buffer = codec.getInputBuffers()[index];
    } catch (IllegalStateException e) {
      Logging.e(TAG, "getInputBuffers failed", e);
      return VideoCodecStatus.ERROR;
    }
    VideoFrame.I420Buffer i420 = videoFrameBuffer.toI420();
    inputColorFormat.fillBufferFromI420(buffer, i420);
    i420.release();

    try {
      codec.queueInputBuffer(
          index, 0 /* offset */, bufferSize, presentationTimestampUs, 0 /* flags */);
    } catch (IllegalStateException e) {
      Logging.e(TAG, "queueInputBuffer failed", e);
      // Keep the output builders in sync with buffers in the codec.
      outputBuilders.pollLast();
      // IllegalStateException thrown when the codec is in the wrong state.
      return VideoCodecStatus.ERROR;
    }
    return VideoCodecStatus.OK;
  }

  @Override
  public VideoCodecStatus setChannelParameters(short packetLoss, long roundTripTimeMs) {
    // No op.
    return VideoCodecStatus.OK;
  }

  @Override
  public VideoCodecStatus setRateAllocation(BitrateAllocation bitrateAllocation, int framerate) {
    if (framerate > MAX_VIDEO_FRAMERATE) {
      framerate = MAX_VIDEO_FRAMERATE;
    }
    bitrateAdjuster.setTargets(bitrateAllocation.getSum(), framerate);
    return updateBitrate();
  }

  @Override
  public ScalingSettings getScalingSettings() {
    // TODO(mellem):  Implement scaling settings.
    return null;
  }

  @Override
  public String getImplementationName() {
    return "HardwareVideoEncoder: " + codecName;
  }

  private VideoCodecStatus resetCodec(int newWidth, int newHeight) {
    VideoCodecStatus status = release();
    if (status != VideoCodecStatus.OK) {
      return status;
    }
    // Zero bitrate and framerate indicate not to change the targets.
    return initEncodeInternal(newWidth, newHeight, 0, 0, callback);
  }

  private boolean shouldForceKeyFrame(long presentationTimestampMs) {
    return forcedKeyFrameMs > 0 && presentationTimestampMs > lastKeyFrameMs + forcedKeyFrameMs;
  }

  private void requestKeyFrame(long presentationTimestampMs) {
    // Ideally MediaCodec would honor BUFFER_FLAG_SYNC_FRAME so we could
    // indicate this in queueInputBuffer() below and guarantee _this_ frame
    // be encoded as a key frame, but sadly that flag is ignored.  Instead,
    // we request a key frame "soon".
    try {
      Bundle b = new Bundle();
      b.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0);
      codec.setParameters(b);
    } catch (IllegalStateException e) {
      Logging.e(TAG, "requestKeyFrame failed", e);
      return;
    }
    lastKeyFrameMs = presentationTimestampMs;
  }

  private Thread createOutputThread() {
    return new Thread() {
      @Override
      public void run() {
        while (running) {
          deliverEncodedImage();
        }
        releaseCodecOnOutputThread();
      }
    };
  }

  private void deliverEncodedImage() {
    try {
      MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
      int index = codec.dequeueOutputBuffer(info, DEQUEUE_OUTPUT_BUFFER_TIMEOUT_US);
      if (index < 0) {
        return;
      }

      ByteBuffer codecOutputBuffer = codec.getOutputBuffers()[index];
      codecOutputBuffer.position(info.offset);
      codecOutputBuffer.limit(info.offset + info.size);

      if ((info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
        Logging.d(TAG, "Config frame generated. Offset: " + info.offset + ". Size: " + info.size);
        configBuffer = ByteBuffer.allocateDirect(info.size);
        configBuffer.put(codecOutputBuffer);
      } else {
        bitrateAdjuster.reportEncodedFrame(info.size);
        if (adjustedBitrate != bitrateAdjuster.getAdjustedBitrateBps()) {
          updateBitrate();
        }

        ByteBuffer frameBuffer;
        boolean isKeyFrame = (info.flags & MediaCodec.BUFFER_FLAG_SYNC_FRAME) != 0;
        if (isKeyFrame && codecType == VideoCodecType.H264) {
          Logging.d(TAG,
              "Prepending config frame of size " + configBuffer.capacity()
                  + " to output buffer with offset " + info.offset + ", size " + info.size);
          // For H.264 key frame prepend SPS and PPS NALs at the start.
          frameBuffer = ByteBuffer.allocateDirect(info.size + configBuffer.capacity());
          configBuffer.rewind();
          frameBuffer.put(configBuffer);
        } else {
          frameBuffer = ByteBuffer.allocateDirect(info.size);
        }
        frameBuffer.put(codecOutputBuffer);
        frameBuffer.rewind();

        EncodedImage.FrameType frameType = EncodedImage.FrameType.VideoFrameDelta;
        if (isKeyFrame) {
          Logging.d(TAG, "Sync frame generated");
          frameType = EncodedImage.FrameType.VideoFrameKey;
        }
        EncodedImage.Builder builder = outputBuilders.poll();
        builder.setBuffer(frameBuffer).setFrameType(frameType);
        // TODO(mellem):  Set codec-specific info.
        callback.onEncodedFrame(builder.createEncodedImage(), new CodecSpecificInfo());
      }
      codec.releaseOutputBuffer(index, false);
    } catch (IllegalStateException e) {
      Logging.e(TAG, "deliverOutput failed", e);
    }
  }

  private void releaseCodecOnOutputThread() {
    Logging.d(TAG, "Releasing MediaCodec on output thread");
    try {
      codec.stop();
    } catch (Exception e) {
      Logging.e(TAG, "Media encoder stop failed", e);
    }
    try {
      codec.release();
    } catch (Exception e) {
      Logging.e(TAG, "Media encoder release failed", e);
      // Propagate exceptions caught during release back to the main thread.
      shutdownException = e;
    }
    Logging.d(TAG, "Release on output thread done");
  }

  private VideoCodecStatus updateBitrate() {
    adjustedBitrate = bitrateAdjuster.getAdjustedBitrateBps();
    try {
      Bundle params = new Bundle();
      params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, adjustedBitrate);
      codec.setParameters(params);
      return VideoCodecStatus.OK;
    } catch (IllegalStateException e) {
      Logging.e(TAG, "updateBitrate failed", e);
      return VideoCodecStatus.ERROR;
    }
  }

  /**
   * Enumeration of supported color formats used for MediaCodec's input.
   */
  private static enum ColorFormat {
    I420 {
      @Override
      void fillBufferFromI420(ByteBuffer buffer, VideoFrame.I420Buffer i420) {
        buffer.put(i420.getDataY());
        buffer.put(i420.getDataU());
        buffer.put(i420.getDataV());
      }
    },
    NV12 {
      @Override
      void fillBufferFromI420(ByteBuffer buffer, VideoFrame.I420Buffer i420) {
        buffer.put(i420.getDataY());

        // Interleave the bytes from the U and V portions, starting with U.
        ByteBuffer u = i420.getDataU();
        ByteBuffer v = i420.getDataV();
        int i = 0;
        while (u.hasRemaining() && v.hasRemaining()) {
          buffer.put(u.get());
          buffer.put(v.get());
        }
      }
    };

    abstract void fillBufferFromI420(ByteBuffer buffer, VideoFrame.I420Buffer i420);

    static ColorFormat valueOf(int colorFormat) {
      switch (colorFormat) {
        case MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Planar:
          return I420;
        case MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420SemiPlanar:
        case MediaCodecInfo.CodecCapabilities.COLOR_QCOM_FormatYUV420SemiPlanar:
        case MediaCodecUtils.COLOR_QCOM_FORMATYUV420PackedSemiPlanar32m:
          return NV12;
        default:
          throw new IllegalArgumentException("Unsupported colorFormat: " + colorFormat);
      }
    }
  }
}
