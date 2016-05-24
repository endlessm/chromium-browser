// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_surface_egl_x11_gles2.h"

#include "ui/gfx/x/x11.h"
#include "ui/gl/egl_util.h"

using ui::GetLastEGLErrorString;
using ui::X11EventSource;

namespace gl {

NativeViewGLSurfaceEGLX11GLES2::NativeViewGLSurfaceEGLX11GLES2(
    EGLNativeWindowType window)
    : NativeViewGLSurfaceEGL(window, nullptr) {}

EGLConfig NativeViewGLSurfaceEGLX11GLES2::GetConfig() {
  if (!config_) {
    // Get a config compatible with the window
    DCHECK(window_);
    XWindowAttributes win_attribs;
    if (!XGetWindowAttributes(GetNativeDisplay(), window_, &win_attribs)) {
      return nullptr;
    }

    // Try matching the window depth with an alpha channel,
    // because we're worried the destination alpha width could
    // constrain blending precision.
    const int kBufferSizeOffset = 1;
    const int kAlphaSizeOffset = 3;
    EGLint config_attribs[] = {
      EGL_BUFFER_SIZE, ~0,
      EGL_ALPHA_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_RED_SIZE, 8,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
      EGL_NONE
    };
    config_attribs[kBufferSizeOffset] = win_attribs.depth;

    EGLDisplay display = GetHardwareDisplay();
    EGLint num_configs;
    if (!eglChooseConfig(display, config_attribs, &config_, 1, &num_configs)) {
      LOG(ERROR) << "eglChooseConfig failed with error "
                 << GetLastEGLErrorString();
      return nullptr;
    }

    if (num_configs) {
      EGLint config_depth;
      if (!eglGetConfigAttrib(display, config_, EGL_BUFFER_SIZE,
                              &config_depth)) {
        LOG(ERROR) << "eglGetConfigAttrib failed with error "
                   << GetLastEGLErrorString();
        return nullptr;
      }

      if (config_depth == win_attribs.depth) {
        return config_;
      }
    }

    // Try without an alpha channel.
    config_attribs[kAlphaSizeOffset] = 0;
    if (!eglChooseConfig(display, config_attribs, &config_, 1, &num_configs)) {
      LOG(ERROR) << "eglChooseConfig failed with error "
                 << GetLastEGLErrorString();
      return nullptr;
    }

    if (num_configs == 0) {
      LOG(ERROR) << "No suitable EGL configs found.";
      return nullptr;
    }
  }
  return config_;
}

NativeViewGLSurfaceEGLX11GLES2::~NativeViewGLSurfaceEGLX11GLES2() {
  Destroy();
}

}  // namespace gl
