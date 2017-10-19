# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import logging
import os

import py_utils
from py_utils import binary_manager
from py_utils import dependency_util
import dependency_manager
from dependency_manager import base_config

from devil import devil_env


from telemetry.core import exceptions
from telemetry.core import util


TELEMETRY_PROJECT_CONFIG = os.path.join(
    util.GetTelemetryDir(), 'telemetry', 'internal', 'binary_dependencies.json')


CHROME_BINARY_CONFIG = os.path.join(util.GetCatapultDir(), 'common', 'py_utils',
                                    'py_utils', 'chrome_binaries.json')


BATTOR_BINARY_CONFIG = os.path.join(util.GetCatapultDir(), 'common', 'battor',
                                    'battor', 'battor_binary_dependencies.json')


NoPathFoundError = dependency_manager.NoPathFoundError
CloudStorageError = dependency_manager.CloudStorageError


_binary_manager = None


def NeedsInit():
  return not _binary_manager


def InitDependencyManager(client_configs):
  global _binary_manager # pylint: disable=global-statement
  if _binary_manager:
    raise exceptions.InitializationError(
        'Trying to re-initialize the binary manager with config %s'
        % client_configs)
  configs = []
  if client_configs:
    configs += client_configs
  configs += [TELEMETRY_PROJECT_CONFIG, CHROME_BINARY_CONFIG]
  _binary_manager = binary_manager.BinaryManager(configs)

  devil_env.config.Initialize()


# Use linux binaries for chromeos.
def _GetOSName(os_name):
  return 'linux' if os_name == 'chromeos' else os_name


def _GetOSForPlatform(platform):
  return _GetOSName(platform.GetOSName())


def FetchPath(binary_name, arch, os_name, os_version=None):
  """ Return a path to the appropriate executable for <binary_name>, downloading
      from cloud storage if needed, or None if it cannot be found.
  """
  if _binary_manager is None:
    raise exceptions.InitializationError(
        'Called FetchPath with uninitialized binary manager.')
  return _binary_manager.FetchPath(
      binary_name, _GetOSName(os_name), arch, os_version)


def LocalPath(binary_name, arch, os_name, os_version=None):
  """ Return a local path to the given binary name, or None if an executable
      cannot be found. Will not download the executable.
      """
  if _binary_manager is None:
    raise exceptions.InitializationError(
        'Called LocalPath with uninitialized binary manager.')
  return _binary_manager.LocalPath(binary_name, os_name, arch, os_version)


def FetchBinaryDependencies(
    platform, client_configs, fetch_reference_chrome_binary):
  """ Fetch all binary dependenencies for the given |platform|.

  Note: we don't fetch browser binaries by default because the size of the
  binary is about 2Gb, and it requires cloud storage permission to
  chrome-telemetry bucket.

  Args:
    platform: an instance of telemetry.core.platform
    client_configs: A list of paths (string) to dependencies json files.
    fetch_reference_chrome_binary: whether to fetch reference chrome binary for
      the given platform.
  """
  configs = [
      dependency_manager.BaseConfig(TELEMETRY_PROJECT_CONFIG),
      dependency_manager.BaseConfig(BATTOR_BINARY_CONFIG)
  ]
  dep_manager = dependency_manager.DependencyManager(configs)
  os_name = _GetOSForPlatform(platform)
  target_platform = '%s_%s' % (os_name, platform.GetArchName())
  dep_manager.PrefetchPaths(target_platform)

  host_platform = None
  fetch_devil_deps = False
  if os_name == 'android':
    host_platform = '%s_%s' % (
        py_utils.GetHostOsName(), py_utils.GetHostArchName())
    dep_manager.PrefetchPaths(host_platform)
    # TODO(aiolos): this is a hack to prefetch the devil deps.
    if host_platform == 'linux_x86_64':
      fetch_devil_deps = True
    else:
      logging.error('Devil only supports 64 bit linux as a host platform. '
                    'Android tests may fail.')

  if fetch_reference_chrome_binary:
    _FetchReferenceBrowserBinary(platform)

  # For now, handle client config separately because the BUILD.gn & .isolate of
  # telemetry tests in chromium src failed to include the files specified in its
  # client config.
  # (https://github.com/catapult-project/catapult/issues/2192)
  # For now this is ok because the client configs usually don't include cloud
  # storage infos.
  # TODO(nednguyen): remove the logic of swallowing exception once the issue is
  # fixed on Chromium side.
  if client_configs:
    manager = dependency_manager.DependencyManager(
        list(dependency_manager.BaseConfig(c) for c in client_configs))
    try:
      manager.PrefetchPaths(target_platform)
      if host_platform is not None:
        manager.PrefetchPaths(host_platform)

    except dependency_manager.NoPathFoundError as e:
      logging.error('Error when trying to prefetch paths for %s: %s',
                    target_platform, e.message)

  if fetch_devil_deps:
    devil_env.config.Initialize()
    devil_env.config.PrefetchPaths(arch=platform.GetArchName())
    devil_env.config.PrefetchPaths()


def _FetchReferenceBrowserBinary(platform):
  os_name = _GetOSForPlatform(platform)
  arch_name = platform.GetArchName()
  manager = binary_manager.BinaryManager(
      [CHROME_BINARY_CONFIG])
  if os_name == 'android':
    os_version = dependency_util.GetChromeApkOsVersion(
        platform.GetOSVersionName())
    manager.FetchPath(
        'chrome_stable', os_name, arch_name, os_version)
  else:
    manager.FetchPath(
        'chrome_stable', os_name, arch_name)


def UpdateDependency(dependency, dep_local_path, version,
                     os_name=None, arch_name=None):
  config = os.path.join(
      util.GetTelemetryDir(), 'telemetry', 'internal',
      'binary_dependencies.json')

  if not os_name:
    assert not arch_name, 'arch_name is specified but not os_name'
    os_name = py_utils.GetHostOsName()
    arch_name = py_utils.GetHostArchName()
  else:
    assert arch_name, 'os_name is specified but not arch_name'

  dep_platform = '%s_%s' % (os_name, arch_name)

  c = base_config.BaseConfig(config, writable=True)
  try:
    old_version = c.GetVersion(dependency, dep_platform)
    print 'Updating from version: {}'.format(old_version)
  except ValueError:
    raise RuntimeError(
        ('binary_dependencies.json entry for %s missing or invalid; please add '
         'it first! (need download_path and path_within_archive)') %
        dep_platform)

  if dep_local_path:
    c.AddCloudStorageDependencyUpdateJob(
        dependency, dep_platform, dep_local_path, version=version,
        execute_job=True)
