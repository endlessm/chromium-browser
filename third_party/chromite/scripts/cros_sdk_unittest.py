# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Tests for cros_sdk."""

from __future__ import print_function

import os

from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import cros_test_lib
from chromite.lib import osutils
from chromite.lib import sudo


# TODO(bmgordon): Figure out how to mock out --create and --enter and then
# add tests that combine those with snapshots.
class CrosSdkSnapshotTest(cros_test_lib.TempDirTestCase):
  """Tests for the snapshot functionality in cros_sdk."""

  def setUp(self):
    with sudo.SudoKeepAlive():
      # Create just enough of a chroot to fool cros_sdk into accepting it.
      self.chroot = os.path.join(self.tempdir, 'chroot')
      cros_build_lib.MountChroot(self.chroot, create=True)
      logging.debug('Chroot mounted on %s', self.chroot)

      chroot_etc = os.path.join(self.chroot, 'etc')
      osutils.SafeMakedirsNonRoot(chroot_etc)

      self.chroot_version_file = os.path.join(chroot_etc, 'cros_chroot_version')
      osutils.Touch(self.chroot_version_file, makedirs=True)

  def tearDown(self):
    with sudo.SudoKeepAlive():
      cros_build_lib.CleanupChrootMount(self.chroot, delete_image=True)

  def _crosSdk(self, args):
    cmd = ['cros_sdk', '--chroot', self.chroot]
    cmd.extend(args)

    try:
      result = cros_build_lib.RunCommand(
          cmd, print_cmd=False, capture_output=True, error_code_ok=True,
          combine_stdout_stderr=True)
    except cros_build_lib.RunCommandError as e:
      raise SystemExit('Running %r failed!: %s' % (cmd, e))

    return result.returncode, result.output

  def testSnapshotsRequireImage(self):
    code, output = self._crosSdk(['--snapshot-list', '--nouse-image'])
    self.assertNotEqual(code, 0)
    self.assertIn('Snapshot operations are not compatible with', output)

    code, output = self._crosSdk(['--snapshot-delete', 'test', '--nouse-image'])
    self.assertNotEqual(code, 0)
    self.assertIn('Snapshot operations are not compatible with', output)

    code, output = self._crosSdk(['--snapshot-create', 'test', '--nouse-image'])
    self.assertNotEqual(code, 0)
    self.assertIn('Snapshot operations are not compatible with', output)

    code, output = self._crosSdk(['--snapshot-restore', 'test',
                                  '--nouse-image'])
    self.assertNotEqual(code, 0)
    self.assertIn('Snapshot operations are not compatible with', output)

  def testSnapshotWithDeleteChroot(self):
    code, output = self._crosSdk(['--delete', '--snapshot-list'])
    self.assertNotEqual(code, 0)
    self.assertIn('Trying to enter or snapshot the chroot', output)

    code, output = self._crosSdk(['--delete', '--snapshot-delete', 'test'])
    self.assertNotEqual(code, 0)
    self.assertIn('Trying to enter or snapshot the chroot', output)

    code, output = self._crosSdk(['--delete', '--snapshot-create', 'test'])
    self.assertNotEqual(code, 0)
    self.assertIn('Trying to enter or snapshot the chroot', output)

    code, output = self._crosSdk(['--delete', '--snapshot-restore', 'test'])
    self.assertNotEqual(code, 0)
    self.assertIn('Trying to enter or snapshot the chroot', output)

  def testEmptySnapshotList(self):
    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output, '')

  def testOneSnapshot(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output.strip(), 'test')

  def testMultipleSnapshots(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    code, _ = self._crosSdk(['--snapshot-create', 'test2'])
    self.assertEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertSetEqual(set(output.strip().split('\n')), {'test', 'test2'})

  def testCantCreateSameSnapshotTwice(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertNotEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output.strip(), 'test')

  def testCreateSnapshotMountsAsNeeded(self):
    with sudo.SudoKeepAlive():
      cros_build_lib.CleanupChrootMount(self.chroot)

    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    self.assertTrue(os.path.exists(self.chroot_version_file))

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output.strip(), 'test')

  def testDeleteGoodSnapshot(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    code, _ = self._crosSdk(['--snapshot-create', 'test2'])
    self.assertEqual(code, 0)

    code, _ = self._crosSdk(['--snapshot-delete', 'test'])
    self.assertEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output.strip(), 'test2')

  def testDeleteMissingSnapshot(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    code, _ = self._crosSdk(['--snapshot-create', 'test2'])
    self.assertEqual(code, 0)

    code, _ = self._crosSdk(['--snapshot-delete', 'test3'])
    self.assertEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertSetEqual(set(output.strip().split('\n')), {'test', 'test2'})

  def testDeleteAndCreateSnapshot(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)
    code, _ = self._crosSdk(['--snapshot-create', 'test2'])
    self.assertEqual(code, 0)

    code, _ = self._crosSdk(['--snapshot-create', 'test',
                             '--snapshot-delete', 'test'])
    self.assertEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertSetEqual(set(output.strip().split('\n')), {'test', 'test2'})

  def testRestoreSnapshot(self):
    with sudo.SudoKeepAlive():
      test_file = os.path.join(self.chroot, 'etc', 'test_file')
      osutils.Touch(test_file)

      code, _ = self._crosSdk(['--snapshot-create', 'test'])
      self.assertEqual(code, 0)

      osutils.SafeUnlink(test_file)

      code, _ = self._crosSdk(['--snapshot-restore', 'test'])
      self.assertEqual(code, 0)
      self.assertTrue(cros_build_lib.MountChroot(self.chroot, create=False))
      self.assertTrue(os.path.exists(test_file))

      code, output = self._crosSdk(['--snapshot-list'])
      self.assertEqual(code, 0)
      self.assertEqual(output, '')

  def testRestoreAndCreateSnapshot(self):
    with sudo.SudoKeepAlive():
      test_file = os.path.join(self.chroot, 'etc', 'test_file')
      osutils.Touch(test_file)

      code, _ = self._crosSdk(['--snapshot-create', 'test'])
      self.assertEqual(code, 0)

      osutils.SafeUnlink(test_file)

      code, _ = self._crosSdk(['--snapshot-restore', 'test',
                               '--snapshot-create', 'test'])
      self.assertEqual(code, 0)
      self.assertTrue(cros_build_lib.MountChroot(self.chroot, create=False))
      self.assertTrue(os.path.exists(test_file))

      code, output = self._crosSdk(['--snapshot-list'])
      self.assertEqual(code, 0)
      self.assertEqual(output.strip(), 'test')

  def testDeleteCantRestoreSameSnapshot(self):
    code, _ = self._crosSdk(['--snapshot-create', 'test'])
    self.assertEqual(code, 0)

    code, _ = self._crosSdk(['--snapshot-delete', 'test',
                             '--snapshot-restore', 'test'])
    self.assertNotEqual(code, 0)

    code, output = self._crosSdk(['--snapshot-list'])
    self.assertEqual(code, 0)
    self.assertEqual(output.strip(), 'test')

  def testCantRestoreInvalidSnapshot(self):
    with sudo.SudoKeepAlive():
      test_file = os.path.join(self.chroot, 'etc', 'test_file')
      osutils.Touch(test_file)

    code, _ = self._crosSdk(['--snapshot-restore', 'test'])
    self.assertNotEqual(code, 0)
    # Failed restore leaves the existing snapshot in place.
    self.assertTrue(os.path.exists(test_file))

  def testRestoreSnapshotMountsAsNeeded(self):
    with sudo.SudoKeepAlive():
      test_file = os.path.join(self.chroot, 'etc', 'test_file')
      osutils.Touch(test_file)

      code, _ = self._crosSdk(['--snapshot-create', 'test'])
      self.assertEqual(code, 0)

      osutils.SafeUnlink(test_file)

      cros_build_lib.CleanupChrootMount(self.chroot)

      code, _ = self._crosSdk(['--snapshot-restore', 'test'])
      self.assertEqual(code, 0)
      self.assertTrue(os.path.exists(test_file))

      code, output = self._crosSdk(['--snapshot-list'])
      self.assertEqual(code, 0)
      self.assertEqual(output, '')
