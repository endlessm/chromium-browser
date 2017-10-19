# Copyright 2017 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""cros tryjob: Schedule a tryjob."""

from __future__ import print_function

import os

from chromite.lib import constants
from chromite.cli import command
from chromite.lib import config_lib
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import path_util

from chromite.cbuildbot import remote_try
from chromite.cbuildbot import trybot_patch_pool


def PrintKnownConfigs(site_config, display_all=False):
  """Print a list of known buildbot configs.

  Args:
    site_config: config_lib.SiteConfig containing all config info.
    display_all: Print all configs.  Otherwise, prints only configs with
                 trybot_list=True.
  """
  def _GetSortKey(config_name):
    config_dict = site_config[config_name]
    return (not config_dict.trybot_list, config_dict.description, config_name)

  COLUMN_WIDTH = 45
  if not display_all:
    print('Note: This is the common list; for all configs, use --all.')
  print('config'.ljust(COLUMN_WIDTH), 'description')
  print('------'.ljust(COLUMN_WIDTH), '-----------')
  config_names = site_config.keys()
  config_names.sort(key=_GetSortKey)
  for name in config_names:
    if display_all or site_config[name].trybot_list:
      desc = site_config[name].description or ''
      print(name.ljust(COLUMN_WIDTH), desc)


def CbuildbotArgs(options):
  """Function to generate cbuidlbot command line args.

  This are pre-api version filtering.

  Args:
    options: Parsed cros tryjob tryjob arguments.

  Returns:
    List of strings in ['arg1', 'arg2'] format.
  """
  args = []

  if options.production:
    args.append('--buildbot')
  else:
    # TODO: Remove from remote_try.py after cbuildbot --remote removed.
    args.append('--remote-trybot')

  if options.branch:
    args.extend(('-b', options.branch))

  for g in options.gerrit_patches:
    args.extend(('-g', g))

  if options.passthrough:
    args.extend(options.passthrough)

  if options.passthrough_raw:
    args.extend(options.passthrough_raw)

  return args


def RunLocal(options):
  """Run a local tryjob."""
  # Create the buildroot, if needed.
  if not os.path.exists(options.buildroot):
    prompt = 'Create %s as buildroot' % options.buildroot
    if not cros_build_lib.BooleanPrompt(prompt=prompt, default=False):
      print('Please specify a different buildroot via the --buildroot option.')
      return 1
    os.makedirs(options.buildroot)

  # Define the command to run.
  launcher = os.path.join(constants.CHROMITE_DIR, 'scripts', 'cbuildbot_launch')
  args = CbuildbotArgs(options)  # The requested build arguments.
  cmd = ([launcher, '--buildroot', options.buildroot] +
         args +
         options.build_configs)

  # Run the tryjob.
  result = cros_build_lib.RunCommand(cmd, debug_level=logging.CRITICAL,
                                     error_code_ok=True, cwd=options.buildroot)
  return result.returncode


def RunRemote(options, patch_pool):
  """Schedule remote tryjobs."""
  logging.info('Scheduling remote tryjob(s): %s',
               ', '.join(options.build_configs))

  # Figure out the cbuildbot command line to pass in.
  args = CbuildbotArgs(options)

  # Figure out the tryjob description.
  description = options.remote_description
  if description is None:
    description = remote_try.DefaultDescription(
        options.branch,
        options.gerrit_patches+options.local_patches)

  print('Submitting tryjob...')
  tryjob = remote_try.RemoteTryJob(options.build_configs,
                                   patch_pool.local_patches,
                                   args,
                                   path_util.GetCacheDir(),
                                   description,
                                   options.committer_email)
  tryjob.Submit(dryrun=False)
  print('Tryjob submitted!')
  print(('Go to %s to view the status of your job.'
         % tryjob.GetTrybotWaterfallLink()))


@command.CommandDecorator('tryjob')
class TryjobCommand(command.CliCommand):
  """Schedule a tryjob."""

  EPILOG = """
Remote Examples:
  cros tryjob -g 123 lumpy-compile-only-pre-cq
  cros tryjob -g 123 -g 456 lumpy-compile-only-pre-cq daisy-pre-cq
  cros tryjob -g 123 --hwtest daisy-paladin

Local Examples:
  cros tryjob --local -g 123 daisy-paladin
  cros tryjob --local --buildroot /my/cool/path -g 123 daisy-paladin

Production Examples (danger, can break production if misused):
  cros tryjob --production --branch release-R61-9765.B asuka-release
  cros tryjob --production --version 9795.0.0 --channel canary  lumpy-payloads
"""

  @classmethod
  def AddParser(cls, parser):
    """Adds a parser."""
    super(cls, TryjobCommand).AddParser(parser)
    parser.add_argument(
        'build_configs', nargs='*',
        help='One or more configs to build.')
    parser.add_argument(
        '-b', '--branch',
        help='The manifest branch to test.  The branch to '
             'check the buildroot out to.')
    parser.add_argument(
        '--yes', action='store_true', default=False,
        help='Never prompt to confirm.')
    parser.add_argument(
        '--production', action='store_true', default=False,
        help='This is a production build, NOT a test build. '
             'Confirm with Chrome OS deputy before use.')
    parser.add_argument(
        '--pass-through', dest='passthrough_raw', action='append',
        help='Arguments to pass to cbuildbot. To be avoided.'
             'Confirm with Chrome OS deputy before use.')

    # Do we build locally, on on a trybot builder?
    where_group = parser.add_argument_group(
        'Where',
        description='Where do we run the tryjob?')
    where_ex = where_group.add_mutually_exclusive_group()
    where_ex.add_argument(
        '--local', action='store_false', dest='remote',
        help='Run the tryjob on your local machine.')
    where_ex.add_argument(
        '--remote', action='store_true', default=True,
        help='Run the tryjob on a remote builder. (default)')
    where_group.add_argument(
        '-r', '--buildroot', type='path', dest='buildroot',
        default=os.path.join(os.path.dirname(constants.SOURCE_ROOT), 'tryjob'),
        help='Root directory to use for the local tryjob. '
             'NOT the current checkout.')

    # What patches do we include in the build?
    what_group = parser.add_argument_group(
        'Patch',
        description='Which patches should be included with the tryjob?')
    what_group.add_argument(
        '-g', '--gerrit-patches', action='append', default=[],
        # metavar='Id1 *int_Id2...IdN',
        help='Space-separated list of short-form Gerrit '
             "Change-Id's or change numbers to patch. "
             "Please prepend '*' to internal Change-Id's")
    what_group.add_argument(
        '-p', '--local-patches', action='append', default=[],
        # metavar="'<project1>[:<branch1>]...<projectN>[:<branchN>]'",
        help='Space-separated list of project branches with '
             'patches to apply.  Projects are specified by name. '
             'If no branch is specified the current branch of the '
             'project will be used.')

    # Identifing the request.
    who_group = parser.add_argument_group(
        'Requestor',
        description='Who is submitting the jobs?')
    who_group.add_argument(
        '--committer-email',
        help='Override default git committer email.')
    who_group.add_argument(
        '--remote-description',
        help='Attach an optional description to a --remote run '
             'to make it easier to identify the results when it '
             'finishes')

    # Modify the build.
    how_group = parser.add_argument_group(
        'Modifiers',
        description='How do we modify build behavior?')
    how_group.add_argument(
        '--latest-toolchain', dest='passthrough', action='append_option',
        help='Use the latest toolchain.')
    how_group.add_argument(
        '--nochromesdk', dest='passthrough', action='append_option',
        help="Don't run the ChromeSDK stage which builds "
             'Chrome outside of the chroot.')
    how_group.add_argument(
        '--timeout', dest='passthrough', action='append_option_value',
        help='Specify the maximum amount of time this job '
             'can run for, at which point the build will be '
             'aborted.  If set to zero, then there is no '
             'timeout.')
    how_group.add_argument(
        '--sanity-check-build', dest='passthrough', action='append_option',
        help='Run the build as a sanity check build.')

    # Overrides for the build configs testing behaviors.
    test_group = parser.add_argument_group(
        'Testing Flags',
        description='How do we change testing behavior?')
    test_group.add_argument(
        '--hwtest', dest='passthrough', action='append_option',
        help='Enable hwlab testing. Default false.')
    test_group.add_argument(
        '--notests', dest='passthrough', action='append_option',
        help='Override values from buildconfig, run no '
             'tests, and build no autotest artifacts.')
    test_group.add_argument(
        '--novmtests', dest='passthrough', action='append_option',
        help='Override values from buildconfig, run no vmtests.')
    test_group.add_argument(
        '--noimagetests', dest='passthrough', action='append_option',
        help='Override values from buildconfig and run no image tests.')

    # <board>-payloads tryjob specific options.
    payloads_group = parser.add_argument_group(
        'Payloads',
        description='Options only used by payloads tryjobs.')
    payloads_group.add_argument(
        '--version', dest='passthrough', action='append_option_value',
        help='Specify the release version for payload regeneration. '
             'Ex: 9799.0.0')
    payloads_group.add_argument(
        '--channel', dest='passthrough', action='append_option_value',
        help='Specify a channel for a payloads trybot. Can '
             'be specified multiple times. No valid for '
             'non-payloads configs.')

    configs_group = parser.add_argument_group(
        'Configs',
        description='Options for displaying available build configs.')
    configs_group.add_argument(
        '-l', '--list', action='store_true', dest='list', default=False,
        help='List the suggested trybot configs to use (see --all)')
    configs_group.add_argument(
        '-a', '--all', action='store_true', dest='list_all', default=False,
        help='List all of the buildbot configs available w/--list')

  def VerifyOptions(self):
    """Verify that our command line options make sense."""
    site_config = config_lib.GetConfig()

    # Handle --list before checking that everything else is valid.
    if self.options.list:
      PrintKnownConfigs(site_config, self.options.list_all)
      raise cros_build_lib.DieSystemExit(0)  # Exit with success code.

    # Validate specified build_configs.
    if not self.options.build_configs:
      cros_build_lib.Die('At least one build_config is required.')

    unknown_build_configs = [b for b in self.options.build_configs
                             if b not in site_config]
    if unknown_build_configs and not self.options.yes:
      prompt = ('Unknown build configs; are you sure you want to schedule '
                'for %s?' % ', '.join(unknown_build_configs))
      if not cros_build_lib.BooleanPrompt(prompt=prompt, default=False):
        cros_build_lib.Die('No confirmation.')

    patches_given = self.options.gerrit_patches or self.options.local_patches

    # Make sure production builds don't have patches.
    if self.options.production:
      if patches_given:
        cros_build_lib.Die('Patches cannot be included in production builds.')
    else:
      # Ask for confirmation if there are no patches to test.
      if not patches_given and not self.options.yes:
        prompt = ('No patches were provided; are you sure you want to just '
                  'run a remote build of %s?' % (
                      self.options.branch if self.options.branch else 'ToT'))
        if not cros_build_lib.BooleanPrompt(prompt=prompt, default=False):
          cros_build_lib.Die('No confirmation.')
    return True

  def Run(self):
    """Runs `cros chroot`."""
    self.options.Freeze()
    self.VerifyOptions()

    # Verify gerrit patches are valid.
    print('Verifying patches...')
    patch_pool = trybot_patch_pool.TrybotPatchPool.FromOptions(
        gerrit_patches=self.options.gerrit_patches,
        local_patches=self.options.local_patches,
        sourceroot=constants.SOURCE_ROOT,
        remote_patches=[])

    if self.options.remote:
      return RunRemote(self.options, patch_pool)
    else:
      return RunLocal(self.options)
