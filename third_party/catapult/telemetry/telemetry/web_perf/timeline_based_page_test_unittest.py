# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from telemetry import decorators
from telemetry.page import page as page_module
from telemetry.testing import browser_test_case
from telemetry.testing import options_for_unittests
from telemetry.testing import page_test_test_case
from telemetry.timeline import chrome_trace_category_filter
from telemetry.util import wpr_modes
from telemetry.web_perf import timeline_based_measurement as tbm_module
from telemetry.web_perf.metrics import smoothness
from tracing.value import histogram
from tracing.value.diagnostics import reserved_infos

class TestTimelinebasedMeasurementPage(page_module.Page):

  def __init__(self, ps, base_dir, trigger_animation=False,
               trigger_jank=False, trigger_slow=False,
               trigger_scroll_gesture=False):
    super(TestTimelinebasedMeasurementPage, self).__init__(
        'file://interaction_enabled_page.html', ps, base_dir,
        name='interaction_enabled_page.html')
    self._trigger_animation = trigger_animation
    self._trigger_jank = trigger_jank
    self._trigger_slow = trigger_slow
    self._trigger_scroll_gesture = trigger_scroll_gesture

  def RunPageInteractions(self, action_runner):
    if self._trigger_animation:
      action_runner.TapElement('#animating-button')
      action_runner.WaitForJavaScriptCondition('window.animationDone')
    if self._trigger_jank:
      action_runner.TapElement('#jank-button')
      action_runner.WaitForJavaScriptCondition('window.jankScriptDone')
    if self._trigger_slow:
      action_runner.TapElement('#slow-button')
      action_runner.WaitForJavaScriptCondition('window.slowScriptDone')
    if self._trigger_scroll_gesture:
      with action_runner.CreateGestureInteraction('Scroll'):
        action_runner.ScrollPage()

class FailedTimelinebasedMeasurementPage(page_module.Page):

  def __init__(self, ps, base_dir):
    super(FailedTimelinebasedMeasurementPage, self).__init__(
        'file://interaction_enabled_page.html', ps, base_dir,
        name='interaction_enabled_page.html')

  def RunPageInteractions(self, action_runner):
    action_runner.TapElement('#does-not-exist')


class TimelineBasedPageTestTest(page_test_test_case.PageTestTestCase):

  def setUp(self):
    browser_test_case.teardown_browser()
    self._options = options_for_unittests.GetCopy()
    self._options.browser_options.wpr_mode = wpr_modes.WPR_OFF

  # This test is flaky when run in parallel on the mac: crbug.com/426676
  # Also, fails on android: crbug.com/437057, and chromeos: crbug.com/483212
  @decorators.Disabled('android', 'mac', 'chromeos')
  @decorators.Disabled('win')  # catapult/issues/2282
  @decorators.Isolated  # Needed because of py_trace_event
  def testSmoothnessTimelineBasedMeasurementForSmoke(self):
    ps = self.CreateEmptyPageSet()
    ps.AddStory(TestTimelinebasedMeasurementPage(
        ps, ps.base_dir, trigger_animation=True))

    options = tbm_module.Options()
    options.SetLegacyTimelineBasedMetrics([smoothness.SmoothnessMetric()])
    tbm = tbm_module.TimelineBasedMeasurement(options)
    results = self.RunMeasurement(tbm, ps, options=self._options)

    self.assertEquals(0, len(results.failures))
    v = results.FindAllPageSpecificValuesFromIRNamed(
        'CenterAnimation', 'frame_time_discrepancy')
    self.assertEquals(len(v), 1)
    v = results.FindAllPageSpecificValuesFromIRNamed(
        'DrawerAnimation', 'frame_time_discrepancy')
    self.assertEquals(len(v), 1)

  # win: crbug.com/520781, chromeos: crbug.com/483212.
  @decorators.Disabled('win', 'chromeos')
  @decorators.Isolated  # Needed because of py_trace_event
  def testTimelineBasedMeasurementGestureAdjustmentSmoke(self):
    ps = self.CreateEmptyPageSet()
    ps.AddStory(TestTimelinebasedMeasurementPage(
        ps, ps.base_dir, trigger_scroll_gesture=True))

    options = tbm_module.Options()
    options.SetLegacyTimelineBasedMetrics([smoothness.SmoothnessMetric()])
    tbm = tbm_module.TimelineBasedMeasurement(options)
    results = self.RunMeasurement(tbm, ps, options=self._options)

    self.assertEquals(0, len(results.failures))
    v = results.FindAllPageSpecificValuesFromIRNamed(
        'Gesture_Scroll', 'frame_time_discrepancy')
    self.assertEquals(len(v), 1)

  @decorators.Disabled('chromeos')
  @decorators.Isolated
  def testTraceCaptureUponFailure(self):
    ps = self.CreateEmptyPageSet()
    ps.AddStory(FailedTimelinebasedMeasurementPage(ps, ps.base_dir))

    options = tbm_module.Options()
    options.config.enable_chrome_trace = True
    options.SetTimelineBasedMetrics(['sampleMetric'])

    tbm = tbm_module.TimelineBasedMeasurement(options)
    results = self.RunMeasurement(tbm, ps, self._options)

    self.assertEquals(1, len(results.failures))
    self.assertEquals(1, len(results.FindAllTraceValues()))

  # Fails on chromeos: crbug.com/483212
  @decorators.Disabled('chromeos')
  @decorators.Isolated
  def testTBM2ForSmoke(self):
    ps = self.CreateEmptyPageSet()
    ps.AddStory(TestTimelinebasedMeasurementPage(ps, ps.base_dir))

    options = tbm_module.Options()
    options.config.enable_chrome_trace = True
    options.SetTimelineBasedMetrics(['sampleMetric'])

    tbm = tbm_module.TimelineBasedMeasurement(options)
    results = self.RunMeasurement(tbm, ps, self._options)

    self.assertEquals(0, len(results.failures))

    self.assertEquals(1, len(results.histograms))
    foos = results.histograms.GetHistogramsNamed('foo')
    self.assertEquals(1, len(foos))
    hist = foos[0]
    benchmarks = hist.diagnostics.get(reserved_infos.BENCHMARKS.name)
    self.assertIsInstance(benchmarks, histogram.GenericSet)
    self.assertEquals(1, len(benchmarks))
    self.assertEquals('', list(benchmarks)[0])
    stories = hist.diagnostics.get(reserved_infos.STORIES.name)
    self.assertIsInstance(stories, histogram.GenericSet)
    self.assertEquals(1, len(stories))
    self.assertEquals('interaction_enabled_page.html', list(stories)[0])
    repeats = hist.diagnostics.get(reserved_infos.STORYSET_REPEATS.name)
    self.assertIsInstance(repeats, histogram.GenericSet)
    self.assertEquals(1, len(repeats))
    self.assertEquals(0, list(repeats)[0])
    hist = list(results.histograms)[0]
    trace_start = hist.diagnostics.get(reserved_infos.TRACE_START.name)
    self.assertIsInstance(trace_start, histogram.DateRange)

    v_foo = results.FindAllPageSpecificValuesNamed('foo_avg')
    self.assertEquals(len(v_foo), 1)
    self.assertEquals(v_foo[0].value, 50)
    self.assertIsNotNone(v_foo[0].page)


  # TODO(ksakamoto): enable this in reference once the reference build of
  # telemetry is updated.
  @decorators.Disabled('reference')
  @decorators.Disabled('chromeos')
  def testFirstPaintMetricSmoke(self):
    ps = self.CreateEmptyPageSet()
    ps.AddStory(TestTimelinebasedMeasurementPage(ps, ps.base_dir))

    cat_filter = chrome_trace_category_filter.ChromeTraceCategoryFilter(
        filter_string='*,blink.console,navigation,blink.user_timing,loading,' +
        'devtools.timeline,disabled-by-default-blink.debug.layout')

    options = tbm_module.Options(overhead_level=cat_filter)
    options.SetTimelineBasedMetrics(['loadingMetric'])

    tbm = tbm_module.TimelineBasedMeasurement(options)
    results = self.RunMeasurement(tbm, ps, self._options)

    self.assertEquals(0, len(results.failures), results.failures)
    v_ttfcp_max = results.FindAllPageSpecificValuesNamed(
        'timeToFirstContentfulPaint_max')
    self.assertEquals(len(v_ttfcp_max), 1)
    self.assertIsNotNone(v_ttfcp_max[0].page)
    self.assertGreater(v_ttfcp_max[0].value, 0)

    v_ttfmp_max = results.FindAllPageSpecificValuesNamed(
        'timeToFirstMeaningfulPaint_max')
    self.assertEquals(len(v_ttfmp_max), 1)
    self.assertIsNotNone(v_ttfmp_max[0].page)
