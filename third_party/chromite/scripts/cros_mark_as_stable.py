# Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""This module uprevs a given package's ebuild to the next revision."""

from __future__ import print_function

import os

from chromite.lib import constants
from chromite.lib import commandline
from chromite.lib import cros_build_lib
from chromite.lib import cros_logging as logging
from chromite.lib import git
from chromite.lib import osutils
from chromite.lib import parallel
from chromite.lib import portage_util

# Commit message subject for uprevving Portage packages.
GIT_COMMIT_SUBJECT = 'Marking set of ebuilds as stable'

# Commit message for uprevving Portage packages.
_GIT_COMMIT_MESSAGE = 'Marking 9999 ebuild for %s as stable.'

# Dictionary of valid commands with usage information.
COMMAND_DICTIONARY = {
    'commit': 'Marks given ebuilds as stable locally',
    'push': 'Pushes previous marking of ebuilds to remote repo',
}


# ======================= Global Helper Functions ========================


def CleanStalePackages(srcroot, boards, package_atoms):
  """Cleans up stale package info from a previous build.

  Args:
    srcroot: Root directory of the source tree.
    boards: Boards to clean the packages from.
    package_atoms: A list of package atoms to unmerge.
  """
  if package_atoms:
    logging.info('Cleaning up stale packages %s.' % package_atoms)

  # First unmerge all the packages for a board, then eclean it.
  # We need these two steps to run in order (unmerge/eclean),
  # but we can let all the boards run in parallel.
  def _CleanStalePackages(board):
    if board:
      suffix = '-' + board
      runcmd = cros_build_lib.RunCommand
    else:
      suffix = ''
      runcmd = cros_build_lib.SudoRunCommand

    emerge, eclean = 'emerge' + suffix, 'eclean' + suffix
    if not osutils.FindMissingBinaries([emerge, eclean]):
      if package_atoms:
        # If nothing was found to be unmerged, emerge will exit(1).
        result = runcmd([emerge, '-q', '--unmerge'] + package_atoms,
                        enter_chroot=True, extra_env={'CLEAN_DELAY': '0'},
                        error_code_ok=True, cwd=srcroot)
        if not result.returncode in (0, 1):
          raise cros_build_lib.RunCommandError('unexpected error', result)
      runcmd([eclean, '-d', 'packages'],
             cwd=srcroot, enter_chroot=True,
             redirect_stdout=True, redirect_stderr=True)

  tasks = []
  for board in boards:
    tasks.append([board])
  tasks.append([None])

  parallel.RunTasksInProcessPool(_CleanStalePackages, tasks)


# TODO(build): This code needs to be gutted and rebased to cros_build_lib.
def _DoWeHaveLocalCommits(stable_branch, tracking_branch, cwd):
  """Returns true if there are local commits."""
  output = git.RunGit(
      cwd, ['rev-parse', stable_branch, tracking_branch]).output.split()
  return output[0] != output[1]


# ======================= End Global Helper Functions ========================


def PushChange(stable_branch, tracking_branch, dryrun, cwd,
               staging_branch=None):
  """Pushes commits in the stable_branch to the remote git repository.

  Pushes local commits from calls to CommitChange to the remote git
  repository specified by current working directory. If changes are
  found to commit, they will be merged to the merge branch and pushed.
  In that case, the local repository will be left on the merge branch.

  Args:
    stable_branch: The local branch with commits we want to push.
    tracking_branch: The tracking branch of the local branch.
    dryrun: Use git push --dryrun to emulate a push.
    cwd: The directory to run commands in.
    staging_branch: The staging branch to push for a failed PFQ run

  Raises:
    OSError: Error occurred while pushing.
  """
  if not git.DoesCommitExistInRepo(cwd, stable_branch):
    logging.debug('No branch created for %s.  Exiting', cwd)
    return

  if not _DoWeHaveLocalCommits(stable_branch, tracking_branch, cwd):
    logging.debug('No work found to push in %s.  Exiting', cwd)
    return

  # For the commit queue, our local branch may contain commits that were
  # just tested and pushed during the CommitQueueCompletion stage. Sync
  # and rebase our local branch on top of the remote commits.
  remote_ref = git.GetTrackingBranch(cwd,
                                     branch=stable_branch,
                                     for_push=True)
  # SyncPushBranch rebases HEAD onto the updated remote. We need to checkout
  # stable_branch here in order to update it.
  git.RunGit(cwd, ['checkout', stable_branch])
  git.SyncPushBranch(cwd, remote_ref.remote, remote_ref.ref)

  # Check whether any local changes remain after the sync.
  if not _DoWeHaveLocalCommits(stable_branch, remote_ref.ref, cwd):
    logging.info('All changes already pushed for %s. Exiting', cwd)
    return

  # Add a failsafe check here.  Only CLs from the 'chrome-bot' user should
  # be involved here.  If any other CLs are found then complain.
  # In dryruns extra CLs are normal, though, and can be ignored.
  bad_cl_cmd = ['log', '--format=short', '--perl-regexp',
                '--author', '^(?!chrome-bot)', '%s..%s' % (
                    remote_ref.ref, stable_branch)]
  bad_cls = git.RunGit(cwd, bad_cl_cmd).output
  if bad_cls.strip() and not dryrun:
    logging.error('The Uprev stage found changes from users other than '
                  'chrome-bot:\n\n%s', bad_cls)
    raise AssertionError('Unexpected CLs found during uprev stage.')

  if staging_branch is not None:
    logging.info('PFQ FAILED. Pushing uprev change to staging branch %s',
                 staging_branch)

  description = git.RunGit(
      cwd,
      ['log', '--format=format:%s%n%n%b',
       '%s..%s' % (remote_ref.ref, stable_branch)]).output
  description = '%s\n\n%s' % (GIT_COMMIT_SUBJECT, description)
  logging.info('For %s, using description %s', cwd, description)
  git.CreatePushBranch(constants.MERGE_BRANCH, cwd,
                       remote_push_branch=remote_ref)
  git.RunGit(cwd, ['merge', '--squash', stable_branch])
  git.RunGit(cwd, ['commit', '-m', description])
  git.RunGit(cwd, ['config', 'push.default', 'tracking'])
  git.PushWithRetry(constants.MERGE_BRANCH, cwd, dryrun=dryrun,
                    staging_branch=staging_branch)


class GitBranch(object):
  """Wrapper class for a git branch."""

  def __init__(self, branch_name, tracking_branch, cwd):
    """Sets up variables but does not create the branch.

    Args:
      branch_name: The name of the branch.
      tracking_branch: The associated tracking branch.
      cwd: The git repository to work in.
    """
    self.branch_name = branch_name
    self.tracking_branch = tracking_branch
    self.cwd = cwd

  def CreateBranch(self):
    self.Checkout()

  def Checkout(self, branch=None):
    """Function used to check out to another GitBranch."""
    if not branch:
      branch = self.branch_name
    if branch == self.tracking_branch or self.Exists(branch):
      git_cmd = ['git', 'checkout', '-f', branch]
    else:
      git_cmd = ['repo', 'start', branch, '.']
    cros_build_lib.RunCommand(git_cmd, print_cmd=False, cwd=self.cwd,
                              capture_output=True)

  def Exists(self, branch=None):
    """Returns True if the branch exists."""
    if not branch:
      branch = self.branch_name
    branches = git.RunGit(self.cwd, ['branch']).output
    return branch in branches.split()


def GetParser():
  """Creates the argparse parser."""
  parser = commandline.ArgumentParser()
  parser.add_argument('--all', action='store_true',
                      help='Mark all packages as stable.')
  parser.add_argument('-b', '--boards', default='',
                      help='Colon-separated list of boards.')
  parser.add_argument('--drop_file',
                      help='File to list packages that were revved.')
  parser.add_argument('--dryrun', action='store_true',
                      help='Passes dry-run to git push if pushing a change.')
  parser.add_argument('--force', action='store_true',
                      help='Force the stabilization of blacklisted packages. '
                      '(only compatible with -p)')
  parser.add_argument('-o', '--overlays',
                      help='Colon-separated list of overlays to modify.')
  parser.add_argument('-p', '--packages',
                      help='Colon separated list of packages to rev.')
  parser.add_argument('-r', '--srcroot', type='path',
                      default=os.path.join(constants.SOURCE_ROOT, 'src'),
                      help='Path to root src directory.')
  parser.add_argument('--verbose', action='store_true',
                      help='Prints out debug info.')
  parser.add_argument('--staging_branch',
                      help='The staging branch to push changes')
  parser.add_argument('command', choices=COMMAND_DICTIONARY.keys(),
                      help='Command to run.')
  return parser


def main(argv):
  parser = GetParser()
  options = parser.parse_args(argv)
  options.Freeze()

  if options.command == 'commit':
    if not options.packages and not options.all:
      parser.error('Please specify at least one package (--packages)')
    if options.force and options.all:
      parser.error('Cannot use --force with --all. You must specify a list of '
                   'packages you want to force uprev.')

  if not os.path.isdir(options.srcroot):
    parser.error('srcroot is not a valid path: %s' % options.srcroot)

  portage_util.EBuild.VERBOSE = options.verbose

  package_list = None
  if options.packages:
    package_list = options.packages.split(':')

  if options.overlays:
    overlays = {}
    for path in options.overlays.split(':'):
      if not os.path.isdir(path):
        cros_build_lib.Die('Cannot find overlay: %s' % path)
      overlays[os.path.realpath(path)] = []
  else:
    logging.warning('Missing --overlays argument')
    overlays = {
        '%s/private-overlays/chromeos-overlay' % options.srcroot: [],
        '%s/third_party/chromiumos-overlay' % options.srcroot: [],
    }

  manifest = git.ManifestCheckout.Cached(options.srcroot)

  if options.command == 'commit':
    portage_util.BuildEBuildDictionary(overlays, options.all, package_list,
                                       allow_blacklisted=options.force)

  # Contains the array of packages we actually revved.
  revved_packages = []
  new_package_atoms = []

  for overlay in overlays:
    ebuilds = overlays[overlay]
    if not os.path.isdir(overlay):
      logging.warning('Skipping %s' % overlay)
      continue

    # Note we intentionally work from the non push tracking branch;
    # everything built thus far has been against it (meaning, http mirrors),
    # thus we should honor that.  During the actual push, the code switches
    # to the correct urls, and does an appropriate rebasing.
    tracking_branch = git.GetTrackingBranchViaManifest(
        overlay, manifest=manifest).ref

    if options.command == 'push':
      PushChange(constants.STABLE_EBUILD_BRANCH, tracking_branch,
                 options.dryrun, cwd=overlay,
                 staging_branch=options.staging_branch)
    elif options.command == 'commit':
      existing_commit = git.GetGitRepoRevision(overlay)
      work_branch = GitBranch(constants.STABLE_EBUILD_BRANCH, tracking_branch,
                              cwd=overlay)
      work_branch.CreateBranch()
      if not work_branch.Exists():
        cros_build_lib.Die('Unable to create stabilizing branch in %s' %
                           overlay)

      # In the case of uprevving overlays that have patches applied to them,
      # include the patched changes in the stabilizing branch.
      git.RunGit(overlay, ['rebase', existing_commit])

      messages = []
      for ebuild in ebuilds:
        if options.verbose:
          logging.info('Working on %s, info %s', ebuild.package,
                       ebuild.cros_workon_vars)
        try:
          new_package = ebuild.RevWorkOnEBuild(options.srcroot, manifest, enforce_subdir_rev=True)
          if new_package:
            revved_packages.append(ebuild.package)
            new_package_atoms.append('=%s' % new_package)
            messages.append(_GIT_COMMIT_MESSAGE % ebuild.package)
        except (OSError, IOError):
          logging.warning(
              'Cannot rev %s\n'
              'Note you will have to go into %s '
              'and reset the git repo yourself.' % (ebuild.package, overlay))
          raise

      if messages:
        portage_util.EBuild.CommitChange('\n\n'.join(messages), overlay)

  if options.command == 'commit':
    chroot_path = os.path.join(options.srcroot, constants.DEFAULT_CHROOT_DIR)
    if os.path.exists(chroot_path):
      CleanStalePackages(options.srcroot, options.boards.split(':'),
                         new_package_atoms)
    if options.drop_file:
      osutils.WriteFile(options.drop_file, ' '.join(revved_packages))
