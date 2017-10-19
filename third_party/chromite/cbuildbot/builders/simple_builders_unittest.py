# Copyright 2015 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Unittests for simpler builders."""

from __future__ import print_function

import copy
import os

from chromite.cbuildbot import cbuildbot_run
from chromite.lib import config_lib
from chromite.lib import constants
from chromite.cbuildbot.builders import generic_builders
from chromite.cbuildbot.builders import simple_builders
from chromite.lib import cros_test_lib
from chromite.lib import osutils
from chromite.lib import parallel
from chromite.scripts import cbuildbot


# pylint: disable=protected-access


class SimpleBuilderTest(cros_test_lib.MockTempDirTestCase):
  """Tests for the main code paths in simple_builders.SimpleBuilder"""

  def setUp(self):
    # List of all stages that would have been called as part of this run.
    self.called_stages = []

    # Simple new function that redirects RunStage to record all stages to be
    # run rather than mock them completely. These can be used in a test to
    # assert something has been called.
    def run_stage(_class_instance, stage_name, *_args, **_kwargs):
      self.called_stages.append(stage_name)

    # Parallel version.
    def run_parallel_stages(_class_instance, *_args):
      # Since parallel stages are forked processes, we can't actually
      # update anything here unless we want to do interprocesses comms.
      pass

    self.buildroot = os.path.join(self.tempdir, 'buildroot')
    chroot_path = os.path.join(self.buildroot, constants.DEFAULT_CHROOT_DIR)
    osutils.SafeMakedirs(os.path.join(chroot_path, 'tmp'))

    self.PatchObject(generic_builders.Builder, '_RunStage',
                     new=run_stage)
    self.PatchObject(simple_builders.SimpleBuilder, '_RunParallelStages',
                     new=run_parallel_stages)
    self.PatchObject(cbuildbot_run._BuilderRunBase, 'GetVersion',
                     return_value='R32-1234.0.0')

    self._manager = parallel.Manager()
    self._manager.__enter__()

  def tearDown(self):
    # Mimic exiting a 'with' statement.
    self._manager.__exit__(None, None, None)

  def _initConfig(
      self, bot_id, extra_argv=None, override_hw_test_config=None, models=None):
    """Return normal options/build_config for |bot_id|"""
    site_config = config_lib.GetConfig()
    build_config = copy.deepcopy(site_config[bot_id])
    build_config['master'] = False
    build_config['important'] = False
    if models:
      build_config['models'] = models

    # Use the cbuildbot parser to create properties and populate default values.
    parser = cbuildbot._CreateParser()
    argv = (['-r', self.buildroot, '--buildbot', '--debug', '--nochromesdk'] +
            (extra_argv if extra_argv else []) + [bot_id])
    options = cbuildbot.ParseCommandLine(parser, argv)

    # Yikes.
    options.managed_chrome = build_config['sync_chrome']

    # Iterate through override and update HWTestConfig attributes.
    if override_hw_test_config:
      for key, val in override_hw_test_config.iteritems():
        for hw_test in build_config.hw_tests:
          setattr(hw_test, key, val)

    return cbuildbot_run.BuilderRun(
        options, site_config, build_config, self._manager)

  def testRunStagesPreCQ(self):
    """Verify RunStages for PRE_CQ_LAUNCHER_TYPE builders"""
    builder_run = self._initConfig('pre-cq-launcher')
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testRunStagesBranchUtil(self):
    """Verify RunStages for CREATE_BRANCH_TYPE builders"""
    extra_argv = ['--branch-name', 'foo', '--version', '1234']
    builder_run = self._initConfig(constants.BRANCH_UTIL_CONFIG,
                                   extra_argv=extra_argv)
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testRunStagesChrootBuilder(self):
    """Verify RunStages for CHROOT_BUILDER_TYPE builders"""
    builder_run = self._initConfig('chromiumos-sdk')
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testRunStagesDefaultBuild(self):
    """Verify RunStages for standard board builders"""
    builder_run = self._initConfig('amd64-generic-full')
    builder_run.attrs.chrome_version = 'TheChromeVersion'
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testRunStagesDefaultBuildCompileCheck(self):
    """Verify RunStages for standard board builders (compile only)"""
    extra_argv = ['--compilecheck']
    builder_run = self._initConfig('amd64-generic-full', extra_argv=extra_argv)
    builder_run.attrs.chrome_version = 'TheChromeVersion'
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testRunStagesDefaultBuildHwTests(self):
    """Verify RunStages for boards w/hwtests"""
    extra_argv = ['--hwtest']
    builder_run = self._initConfig('lumpy-release', extra_argv=extra_argv)
    builder_run.attrs.chrome_version = 'TheChromeVersion'
    simple_builders.SimpleBuilder(builder_run).RunStages()

  def testThatWeScheduleHWTestsRegardlessOfBlocking(self):
    """Verify RunStages for boards w/hwtests (blocking).

    Make sure the same stages get scheduled regardless of whether their hwtest
    suites are marked blocking or not.
    """
    extra_argv = ['--hwtest']
    builder_run_without_blocking = self._initConfig(
        'lumpy-release', extra_argv=extra_argv,
        override_hw_test_config=dict(blocking=False))
    builder_run_with_blocking = self._initConfig(
        'lumpy-release', extra_argv=extra_argv,
        override_hw_test_config=dict(blocking=True))
    builder_run_without_blocking.attrs.chrome_version = 'TheChromeVersion'
    builder_run_with_blocking.attrs.chrome_version = 'TheChromeVersion'

    simple_builders.SimpleBuilder(builder_run_without_blocking).RunStages()
    without_blocking_stages = list(self.called_stages)

    self.called_stages = []
    simple_builders.SimpleBuilder(builder_run_with_blocking).RunStages()
    self.assertEqual(without_blocking_stages, self.called_stages)

  def testUnifiedBuildsRunHwTestsForAllModels(self):
    """Verify hwtests run for model fanout with unified builds"""
    extra_argv = ['--hwtest']
    unified_build = self._initConfig(
        'lumpy-release',
        extra_argv=extra_argv,
        models=[config_lib.ModelTestConfig('model1'),
                config_lib.ModelTestConfig('model2', ['sanity', 'bvt-inline'])])
    unified_build.attrs.chrome_version = 'TheChromeVersion'
    simple_builders.SimpleBuilder(unified_build).RunStages()

  def testGetHWTestStageWithPerModelFilters(self):
    """Verify hwtests are filtered correctly on a per-model basis"""
    extra_argv = ['--hwtest']
    unified_build = self._initConfig(
        'lumpy-release',
        extra_argv=extra_argv)
    unified_build.attrs.chrome_version = 'TheChromeVersion'

    test_phase1 = unified_build.config.hw_tests[0]
    test_phase2 = unified_build.config.hw_tests[1]

    model1 = config_lib.ModelTestConfig('model1')
    model2 = config_lib.ModelTestConfig('model2', [test_phase2.suite])

    hw_stage = simple_builders.SimpleBuilder(unified_build)._GetHWTestStage(
        unified_build,
        'lumpy',
        model1,
        test_phase1)
    self.assertIsNotNone(hw_stage)

    hw_stage = simple_builders.SimpleBuilder(unified_build)._GetHWTestStage(
        unified_build,
        'lumpy',
        model2,
        test_phase1)
    self.assertIsNone(hw_stage)

    hw_stage = simple_builders.SimpleBuilder(unified_build)._GetHWTestStage(
        unified_build,
        'lumpy',
        model2,
        test_phase2)
    self.assertIsNotNone(hw_stage)
