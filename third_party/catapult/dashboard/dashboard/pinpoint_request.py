# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""URL endpoint containing server-side functionality for pinpoint jobs."""

import json

from google.appengine.api import users

from dashboard import start_try_job
from dashboard.common import namespaced_stored_object
from dashboard.common import request_handler
from dashboard.common import utils
from dashboard.services import pinpoint_service

_BOTS_TO_DIMENSIONS = 'bot_dimensions_map'
_PINPOINT_REPOSITORIES = 'repositories'


class InvalidParamsError(Exception):
  pass


class PinpointNewRequestHandler(request_handler.RequestHandler):
  def post(self):
    job_params = dict(
        (a, self.request.get(a)) for a in self.request.arguments())

    try:
      pinpoint_params = PinpointParamsFromBisectParams(job_params)
    except InvalidParamsError as e:
      self.response.write(json.dumps({'error': e.message}))
      return

    self.response.write(json.dumps(pinpoint_service.NewJob(pinpoint_params)))


def PinpointParamsFromBisectParams(params):
  """Takes parameters from Dashboard's pinpoint-job-dialog and returns
  a dict with parameters for a new Pinpoint job.

  Args:
    params: A dict in the following format:
    {
        'test_path': Test path for the metric being bisected.
        'start_git_hash': Git hash of earlier revision.
        'end_git_hash': Git hash of later revision.
        'start_repository': Repository for earlier revision.
        'end_repository': Repository for later revision.
        'bug_id': Associated bug.
    }

  Returns:
    A dict of params for passing to Pinpoint to start a job, or a dict with an
    'error' field.
  """
  if not utils.IsValidSheriffUser():
    user = users.get_current_user()
    raise InvalidParamsError('User "%s" not authorized.' % user)

  bots_to_dimensions = namespaced_stored_object.Get(_BOTS_TO_DIMENSIONS)

  # Pinpoint takes swarming dimensions, so we need to map bot name to those.
  test_path = params.get('test_path')
  test_path_parts = test_path.split('/')
  bot_name = test_path_parts[1]
  suite = test_path_parts[2]
  metric = test_path_parts[-1]

  dimensions = bots_to_dimensions.get(bot_name)
  if not dimensions:
    raise InvalidParamsError('No dimensions for bot %s defined.' % bot_name)

  # Pinpoint also requires you specify which isolate target to run the
  # test, so we derive that from the suite name. Eventually, this would
  # ideally be stored in a SparesDiagnostic but for now we can guess.
  isolate_targets = [
      'angle_perftests', 'cc_perftests', 'gpu_perftests',
      'load_library_perf_tests', 'media_perftests', 'net_perftests',
      'performance_browser_tests', 'telemetry_perf_tests',
      'telemetry_perf_webview_tests', 'tracing_perftests']

  target = 'telemetry_perf_tests'
  if suite in isolate_targets:
    target = suite
  elif 'webview' in bot_name:
    target = 'telemetry_perf_webview_tests'

  start_repository = params.get('start_repository')
  end_repository = params.get('end_repository')
  start_git_hash = params.get('start_git_hash')
  end_git_hash = params.get('end_git_hash')

  supported_repositories = namespaced_stored_object.Get(_PINPOINT_REPOSITORIES)

  # Bail if it's not a supported repository to bisect on
  if not start_repository in supported_repositories:
    raise InvalidParamsError('Invalid repository: %s' % start_repository)
  if not end_repository in supported_repositories:
    raise InvalidParamsError('Invalid repository: %s' % end_repository)

  # Pinpoint only supports chromium at the moment, so just throw up a
  # different error for now.
  if start_repository != 'chromium' or end_repository != 'chromium':
    raise InvalidParamsError('Only chromium bisects supported currently.')

  email = users.get_current_user().email()
  job_name = 'Job on [%s/%s/%s] for [%s]' % (bot_name, suite, metric, email)

  browser = start_try_job.GuessBrowserName(bot_name)

  return {
      'configuration': bot_name,
      'browser': browser,
      'benchmark': suite,
      'test': params.get('test'),
      'metric': metric,
      'start_repository': start_repository,
      'end_repository': end_repository,
      'start_git_hash': start_git_hash,
      'end_git_hash': end_git_hash,
      'bug_id': params.get('bug_id'),
      'auto_explore': '1',
      'target': target,
      'dimensions': json.dumps(dimensions),
      'email': email,
      'name': job_name
  }
