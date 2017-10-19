# Copyright 2017 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json

from dashboard.pinpoint.models import quest as quest_module


_DEFAULT_REPEAT_COUNT = 20

_SWARMING_EXTRA_ARGS = (
    '--isolated-script-test-output', '${ISOLATED_OUTDIR}/output.json',
    '--isolated-script-test-chartjson-output',
    '${ISOLATED_OUTDIR}/chartjson-output.json',
)


def GenerateQuests(request):
  """Generate a list of Quests from a request.

  GenerateQuests uses the request parameters to infer what types of Quests the
  user wants to run, and creates a list of Quests with the given configuration.

  Arguments:
    request: A WebOb/webapp2 Request object.

  Returns:
    A tuple of (arguments, quests), where arguments is a dict containing the
    request arguments that were used, and quests is a list of Quests.
  """
  target = request.get('target')
  if target in ('telemetry_perf_tests', 'telemetry_perf_webview_tests'):
    quest_functions = (_FindIsolate, _TelemetryRunTest, _ReadChartJsonValue)
  else:
    quest_functions = (_FindIsolate, _GTestRunTest, _ReadGraphJsonValue)

  arguments = {}
  quests = []
  for quest_function in quest_functions:
    quest_arguments, quest = quest_function(request)
    if not quest:
      return arguments, quests
    arguments.update(quest_arguments)
    quests.append(quest)

  return arguments, quests


def _FindIsolate(request):
  arguments = {}

  configuration = request.get('configuration')
  if not configuration:
    raise TypeError('Missing "configuration" argument.')
  arguments['configuration'] = configuration

  target = request.get('target')
  if not target:
    raise TypeError('Missing "target" argument.')
  arguments['target'] = target

  return arguments, quest_module.FindIsolate(configuration, target)


def _TelemetryRunTest(request):
  arguments = {}
  swarming_extra_args = []

  dimensions = request.get('dimensions')
  if not dimensions:
    return {}, None
  dimensions = json.loads(dimensions)
  arguments['dimensions'] = json.dumps(dimensions)

  benchmark = request.get('benchmark')
  if not benchmark:
    raise TypeError('Missing "benchmark" argument.')
  arguments['benchmark'] = benchmark
  swarming_extra_args.append(benchmark)

  story = request.get('story')
  if story:
    arguments['story'] = story
    swarming_extra_args += ('--story-filter', story)

  repeat_count = request.get('repeat_count')
  if repeat_count:
    arguments['repeat_count'] = repeat_count
  else:
    repeat_count = str(_DEFAULT_REPEAT_COUNT)
  swarming_extra_args += ('--pageset-repeat', repeat_count)

  browser = request.get('browser')
  if not browser:
    raise TypeError('Missing "browser" argument.')
  arguments['browser'] = browser
  swarming_extra_args += ('--browser', browser)

  swarming_extra_args += ('-v', '--upload-results',
                          '--output-format', 'chartjson')
  swarming_extra_args += _SWARMING_EXTRA_ARGS

  return arguments, quest_module.RunTest(dimensions, swarming_extra_args)


def _GTestRunTest(request):
  arguments = {}
  swarming_extra_args = []

  dimensions = request.get('dimensions')
  if not dimensions:
    return {}, None
  dimensions = json.loads(dimensions)
  arguments['dimensions'] = json.dumps(dimensions)

  test = request.get('test')
  if test:
    arguments['test'] = test
    swarming_extra_args += ('--gtest_filter', test)

  repeat_count = request.get('repeat_count')
  if repeat_count:
    arguments['repeat_count'] = repeat_count
  else:
    repeat_count = str(_DEFAULT_REPEAT_COUNT)
  swarming_extra_args += ('--gtest_repeat', repeat_count)

  swarming_extra_args += _SWARMING_EXTRA_ARGS

  return arguments, quest_module.RunTest(dimensions, swarming_extra_args)


def _ReadChartJsonValue(request):
  arguments = {}

  chart = request.get('chart')
  if not chart:
    return {}, None
  arguments['chart'] = chart

  tir_label = request.get('tir_label')
  if tir_label:
    arguments['tir_label'] = tir_label

  trace = request.get('trace')
  if trace:
    arguments['trace'] = trace

  return arguments, quest_module.ReadChartJsonValue(chart, tir_label, trace)


def _ReadGraphJsonValue(request):
  arguments = {}

  chart = request.get('chart')
  trace = request.get('trace')
  if not (chart or trace):
    return {}, None
  if chart and not trace:
    raise TypeError('"chart" specified but no "trace" given.')
  if trace and not chart:
    raise TypeError('"trace" specified but no "chart" given.')
  arguments['chart'] = chart
  arguments['trace'] = trace

  return arguments, quest_module.ReadGraphJsonValue(chart, trace)
