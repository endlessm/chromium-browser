#!/usr/bin/env python
# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import subprocess


def GetBinaryPath():
  return '/usr/lib/nodejs-mozilla/bin/node'


def RunNode(cmd_parts, stdout=None):
  cmd = " ".join([GetBinaryPath()] + cmd_parts)
  process = subprocess.Popen(
      cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
  stdout, stderr = process.communicate()

  if stderr:
    raise RuntimeError('%s failed: %s' % (cmd, stderr))

  return stdout
