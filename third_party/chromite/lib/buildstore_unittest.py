# -*- coding: utf-8 -*-
# Copyright 2018 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Unittests for buildstore library."""

from __future__ import print_function

import mock

from chromite.lib import cidb
from chromite.lib import constants
from chromite.lib import cros_test_lib
from chromite.lib import buildstore

BuildStore = buildstore.BuildStore


class TestBuildStore(cros_test_lib.MockTestCase):
  """Test buildstore.BuildStore."""

  def testIsCIDBClientMissing(self):
    """Tests _IsCIDBClientMissing function."""
    # pylint: disable=protected-access
    # Test CIDB needed and client missing.
    bs = BuildStore(_read_from_bb=False, _write_to_cidb=True)
    self.assertEqual(bs._IsCIDBClientMissing(), True)
    bs = BuildStore(_read_from_bb=True, _write_to_cidb=True)
    self.assertEqual(bs._IsCIDBClientMissing(), True)
    bs = BuildStore(_read_from_bb=False, _write_to_cidb=False)
    self.assertEqual(bs._IsCIDBClientMissing(), True)
    # Test CIDB is needed and client is up and running.
    bs = BuildStore(_read_from_bb=False, _write_to_cidb=True)
    bs.cidb_conn = object()
    self.assertEqual(bs._IsCIDBClientMissing(), False)
    bs = BuildStore(_read_from_bb=True, _write_to_cidb=True)
    bs.cidb_conn = object()
    self.assertEqual(bs._IsCIDBClientMissing(), False)
    bs = BuildStore(_read_from_bb=False, _write_to_cidb=False)
    bs.cidb_conn = object()
    self.assertEqual(bs._IsCIDBClientMissing(), False)
    # Test CIDB is not needed.
    bs = BuildStore(_read_from_bb=True, _write_to_cidb=False)
    self.assertEqual(bs._IsCIDBClientMissing(), False)

  def testInitializeClientsWithCIDBSetup(self):
    """Tests InitializeClients with mock CIDB."""

    class DummyCIDBConnection(object):
      """Dummy class representing CIDBConnection."""

    # With CIDB setup, cidb_conn is populated.
    self.PatchObject(cidb.CIDBConnectionFactory, 'IsCIDBSetup',
                     return_value=True)
    mock_cidb = DummyCIDBConnection()
    self.PatchObject(cidb.CIDBConnectionFactory,
                     'GetCIDBConnectionForBuilder',
                     return_value=mock_cidb)
    bs = BuildStore()
    result = bs.InitializeClients()
    self.assertEqual(bs.cidb_conn, mock_cidb)
    self.assertEqual(result, True)

  def testInitializeClientsWithoutCIDBSetup(self):
    """Tests InitializeClients with mock CIDB."""

    self.PatchObject(cidb.CIDBConnectionFactory, 'IsCIDBSetup',
                     return_value=False)
    bs = BuildStore()
    self.assertEqual(bs.InitializeClients(), False)

  def testInitializeClientsWhenCIDBIsNotNeeded(self):
    """Test InitializeClients without CIDB requirement."""
    bs = BuildStore(_read_from_bb=True, _write_to_cidb=False)
    bs.cidb_conn = None
    # Does not raise exception.
    self.assertEqual(bs.InitializeClients(), True)

  def testInsertBuild(self):
    """Tests the redirect for InsertBuild function."""
    self.PatchObject(BuildStore, 'InitializeClients',
                     return_value=True)
    # Test CIDB redirect.
    bs = BuildStore(_write_to_cidb=True, _write_to_bb=False)
    bs.cidb_conn = mock.MagicMock()
    self.PatchObject(bs.cidb_conn, 'InsertBuild',
                     return_value=constants.MOCK_BUILD_ID)
    build_id = bs.InsertBuild(
        'builder_name', 12345,
        'something-paladin', 'bot_hostname', master_build_id='master_id',
        timeout_seconds='timeout')
    bs.cidb_conn.InsertBuild.assert_called_once_with(
        'builder_name', 12345, 'something-paladin', 'bot_hostname',
        'master_id', 'timeout', None, None, None)
    self.assertEqual(build_id, constants.MOCK_BUILD_ID)

  def testInsertBuildStage(self):
    """Tests the redirect for InsertBuildStage function."""
    self.PatchObject(BuildStore, 'InitializeClients',
                     return_value=True)
    bs = BuildStore()
    bs.cidb_conn = mock.MagicMock()
    self.PatchObject(bs.cidb_conn, 'InsertBuildStage',
                     return_value=constants.MOCK_STAGE_ID)
    build_stage_id = bs.InsertBuildStage(
        constants.MOCK_BUILD_ID, 'stage_name')
    bs.cidb_conn.InsertBuildStage.assert_called_once_with(
        constants.MOCK_BUILD_ID, 'stage_name', None,
        constants.BUILDER_STATUS_PLANNED)
    self.assertEqual(build_stage_id, constants.MOCK_STAGE_ID)

  def testFinishBuild(self):
    """Tests the redirect for FinishBuild function."""
    self.PatchObject(BuildStore, 'InitializeClients',
                     return_value=True)
    bs = BuildStore()
    bs.cidb_conn = mock.MagicMock()
    status = mock.Mock()
    summary = mock.Mock()
    metadata_url = mock.Mock()
    strict = mock.Mock()
    self.PatchObject(bs.cidb_conn, 'FinishBuild')
    bs.FinishBuild(constants.MOCK_BUILD_ID, status=status, summary=summary,
                   metadata_url=metadata_url, strict=strict)
    bs.cidb_conn.FinishBuild.assert_called_once_with(
        constants.MOCK_BUILD_ID, status=status, summary=summary,
        metadata_url=metadata_url, strict=strict)

  def testExtendDeadline(self):
    """Tests the redirect for ExtendDeadline function."""
    self.PatchObject(BuildStore, 'InitializeClients',
                     return_value=True)
    bs = BuildStore()
    bs.cidb_conn = mock.MagicMock()
    mock_timeout = mock.Mock()
    self.PatchObject(bs.cidb_conn, 'ExtendDeadline')
    bs.ExtendDeadline(constants.MOCK_BUILD_ID, mock_timeout)
    bs.cidb_conn.ExtendDeadline.assert_called_once_with(
        constants.MOCK_BUILD_ID, mock_timeout)

  def testGetBuildStatuses(self):
    """Tests the redirect for GetBuildStatuses function."""
    self.PatchObject(BuildStore, 'InitializeClients',
                     return_value=True)
    bs = BuildStore()
    bs.cidb_conn = mock.MagicMock()
    build_ids = ['build 1', 'build 2']
    buildbucket_ids = ['bucket 1', 'bucket 2']
    # Test for buildbucket_ids.
    bs.GetBuildStatuses(buildbucket_ids)
    bs.cidb_conn.GetBuildStatusesWithBuildbucketIds.assert_called_once_with(
        buildbucket_ids)
    # Test for build_ids.
    bs.GetBuildStatuses(build_ids=build_ids)
    bs.cidb_conn.GetBuildStatuses.assert_called_once_with(build_ids)
    # Test for neither arguments.
    self.assertEqual(bs.GetBuildStatuses(), [])
