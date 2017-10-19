#!/usr/bin/env python
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# TODO(hinoka): Use logging.

import cStringIO
import codecs
import copy
import ctypes
import json
import optparse
import os
import pprint
import random
import re
import subprocess
import sys
import tempfile
import threading
import time
import urllib2
import urlparse
import uuid

import os.path as path

# How many bytes at a time to read from pipes.
BUF_SIZE = 256


# Define a bunch of directory paths.
# Relative to this script's filesystem path.
THIS_DIR = path.dirname(path.abspath(__file__))

DEPOT_TOOLS_DIR = path.abspath(path.join(THIS_DIR, '..', '..', '..', '..'))

CHROMIUM_GIT_HOST = 'https://chromium.googlesource.com'
CHROMIUM_SRC_URL = CHROMIUM_GIT_HOST + '/chromium/src.git'

BRANCH_HEADS_REFSPEC = '+refs/branch-heads/*'
TAGS_REFSPEC = '+refs/tags/*'

# Regular expression that matches a single commit footer line.
COMMIT_FOOTER_ENTRY_RE = re.compile(r'([^:]+):\s*(.*)')

# Footer metadata keys for regular and gsubtreed mirrored commit positions.
COMMIT_POSITION_FOOTER_KEY = 'Cr-Commit-Position'
COMMIT_ORIGINAL_POSITION_FOOTER_KEY = 'Cr-Original-Commit-Position'

# Regular expression to parse gclient's revinfo entries.
REVINFO_RE = re.compile(r'^([^:]+):\s+([^@]+)@(.+)$')


# Copied from scripts/recipes/chromium.py.
GOT_REVISION_MAPPINGS = {
    CHROMIUM_SRC_URL: {
        'got_revision': 'src/',
        'got_nacl_revision': 'src/native_client/',
        'got_swarm_client_revision': 'src/tools/swarm_client/',
        'got_swarming_client_revision': 'src/tools/swarming_client/',
        'got_v8_revision': 'src/v8/',
        'got_webkit_revision': 'src/third_party/WebKit/',
        'got_webrtc_revision': 'src/third_party/webrtc/',
    }
}


GCLIENT_TEMPLATE = """solutions = %(solutions)s

cache_dir = r%(cache_dir)s
%(target_os)s
%(target_os_only)s
"""


# How many times to try before giving up.
ATTEMPTS = 5

GIT_CACHE_PATH = path.join(DEPOT_TOOLS_DIR, 'git_cache.py')
GCLIENT_PATH = path.join(DEPOT_TOOLS_DIR, 'gclient.py')

# If there is less than 100GB of disk space on the system, then we do
# a shallow checkout.
SHALLOW_CLONE_THRESHOLD = 100 * 1024 * 1024 * 1024


class SubprocessFailed(Exception):
  def __init__(self, message, code, output):
    Exception.__init__(self, message)
    self.code = code
    self.output = output


class PatchFailed(SubprocessFailed):
  pass


class GclientSyncFailed(SubprocessFailed):
  pass


class InvalidDiff(Exception):
  pass


RETRY = object()
OK = object()
FAIL = object()


class PsPrinter(object):
  def __init__(self, interval=300):
    self.interval = interval
    self.active = sys.platform.startswith('linux2')
    self.thread = None

  @staticmethod
  def print_pstree():
    """Debugging function used to print "ps auxwwf" for stuck processes."""
    subprocess.call(['ps', 'auxwwf'])

  def poke(self):
    if self.active:
      self.cancel()
      self.thread = threading.Timer(self.interval, self.print_pstree)
      self.thread.start()

  def cancel(self):
    if self.active and self.thread is not None:
      self.thread.cancel()
      self.thread = None


def call(*args, **kwargs):  # pragma: no cover
  """Interactive subprocess call."""
  kwargs['stdout'] = subprocess.PIPE
  kwargs['stderr'] = subprocess.STDOUT
  kwargs.setdefault('bufsize', BUF_SIZE)
  cwd = kwargs.get('cwd', os.getcwd())
  stdin_data = kwargs.pop('stdin_data', None)
  if stdin_data:
    kwargs['stdin'] = subprocess.PIPE
  out = cStringIO.StringIO()
  new_env = kwargs.get('env', {})
  env = copy.copy(os.environ)
  env.update(new_env)
  kwargs['env'] = env

  if new_env:
    print '===Injecting Environment Variables==='
    for k, v in sorted(new_env.items()):
      print '%s: %s' % (k, v)
  print '===Running %s===' % (' '.join(args),)
  print 'In directory: %s' % cwd
  start_time = time.time()
  proc = subprocess.Popen(args, **kwargs)
  if stdin_data:
    proc.stdin.write(stdin_data)
    proc.stdin.close()
  psprinter = PsPrinter()
  # This is here because passing 'sys.stdout' into stdout for proc will
  # produce out of order output.
  hanging_cr = False
  while True:
    psprinter.poke()
    buf = proc.stdout.read(BUF_SIZE)
    if not buf:
      break
    if hanging_cr:
      buf = '\r' + buf
    hanging_cr = buf.endswith('\r')
    if hanging_cr:
      buf = buf[:-1]
    buf = buf.replace('\r\n', '\n').replace('\r', '\n')
    sys.stdout.write(buf)
    out.write(buf)
  if hanging_cr:
    sys.stdout.write('\n')
    out.write('\n')
  psprinter.cancel()

  code = proc.wait()
  elapsed_time = ((time.time() - start_time) / 60.0)
  outval = out.getvalue()
  if code:
    print '===Failed in %.1f mins===' % elapsed_time
    print
    raise SubprocessFailed('%s failed with code %d in %s.' %
                           (' '.join(args), code, cwd),
                           code, outval)

  print '===Succeeded in %.1f mins===' % elapsed_time
  print
  return outval


def git(*args, **kwargs):  # pragma: no cover
  """Wrapper around call specifically for Git commands."""
  if args and args[0] == 'cache':
    # Rewrite "git cache" calls into "python git_cache.py".
    cmd = (sys.executable, '-u', GIT_CACHE_PATH) + args[1:]
  else:
    git_executable = 'git'
    # On windows, subprocess doesn't fuzzy-match 'git' to 'git.bat', so we
    # have to do it explicitly. This is better than passing shell=True.
    if sys.platform.startswith('win'):
      git_executable += '.bat'
    cmd = (git_executable,) + args
  return call(*cmd, **kwargs)


def get_gclient_spec(solutions, target_os, target_os_only, git_cache_dir):
  return GCLIENT_TEMPLATE % {
      'solutions': pprint.pformat(solutions, indent=4),
      'cache_dir': '"%s"' % git_cache_dir,
      'target_os': ('\ntarget_os=%s' % target_os) if target_os else '',
      'target_os_only': '\ntarget_os_only=%s' % target_os_only
  }


def solutions_printer(solutions):
  """Prints gclient solution to stdout."""
  print 'Gclient Solutions'
  print '================='
  for solution in solutions:
    name = solution.get('name')
    url = solution.get('url')
    print '%s (%s)' % (name, url)
    if solution.get('deps_file'):
      print '  Dependencies file is %s' % solution['deps_file']
    if 'managed' in solution:
      print '  Managed mode is %s' % ('ON' if solution['managed'] else 'OFF')
    custom_vars = solution.get('custom_vars')
    if custom_vars:
      print '  Custom Variables:'
      for var_name, var_value in sorted(custom_vars.iteritems()):
        print '    %s = %s' % (var_name, var_value)
    custom_deps = solution.get('custom_deps')
    if 'custom_deps' in solution:
      print '  Custom Dependencies:'
      for deps_name, deps_value in sorted(custom_deps.iteritems()):
        if deps_value:
          print '    %s -> %s' % (deps_name, deps_value)
        else:
          print '    %s: Ignore' % deps_name
    for k, v in solution.iteritems():
      # Print out all the keys we don't know about.
      if k in ['name', 'url', 'deps_file', 'custom_vars', 'custom_deps',
               'managed']:
        continue
      print '  %s is %s' % (k, v)
    print


def modify_solutions(input_solutions):
  """Modifies urls in solutions to point at Git repos.

  returns: new solution dictionary
  """
  assert input_solutions
  solutions = copy.deepcopy(input_solutions)
  for solution in solutions:
    original_url = solution['url']
    parsed_url = urlparse.urlparse(original_url)
    parsed_path = parsed_url.path

    solution['managed'] = False
    # We don't want gclient to be using a safesync URL. Instead it should
    # using the lkgr/lkcr branch/tags.
    if 'safesync_url' in solution:
      print 'Removing safesync url %s from %s' % (solution['safesync_url'],
                                                  parsed_path)
      del solution['safesync_url']

  return solutions


def remove(target, cleanup_dir):
  """Remove a target by moving it into cleanup_dir."""
  if not path.exists(cleanup_dir):
    os.makedirs(cleanup_dir)
  dest = path.join(cleanup_dir, '%s_%s' % (
      path.basename(target), uuid.uuid4().hex))
  print 'Marking for removal %s => %s' % (target, dest)
  try:
    os.rename(target, dest)
  except Exception as e:
    print 'Error renaming %s to %s: %s' % (target, dest, str(e))
    raise


def ensure_no_checkout(dir_names, cleanup_dir):
  """Ensure that there is no undesired checkout under build/."""
  build_dir = os.getcwd()
  has_checkout = any(path.exists(path.join(build_dir, dir_name, '.git'))
                     for dir_name in dir_names)
  if has_checkout:
    for filename in os.listdir(build_dir):
      deletion_target = path.join(build_dir, filename)
      print '.git detected in checkout, deleting %s...' % deletion_target,
      remove(deletion_target, cleanup_dir)
      print 'done'


def call_gclient(*args, **kwargs):
  """Run the "gclient.py" tool with the supplied arguments.

  Args:
    args: command-line arguments to pass to gclient.
    kwargs: keyword arguments to pass to call.
  """
  cmd = [sys.executable, '-u', GCLIENT_PATH]
  cmd.extend(args)
  return call(*cmd, **kwargs)


def gclient_configure(solutions, target_os, target_os_only, git_cache_dir):
  """Should do the same thing as gclient --spec='...'."""
  with codecs.open('.gclient', mode='w', encoding='utf-8') as f:
    f.write(get_gclient_spec(
        solutions, target_os, target_os_only, git_cache_dir))


def gclient_sync(
    with_branch_heads, with_tags, shallow, revisions, break_repo_locks,
    disable_syntax_validation):
  # We just need to allocate a filename.
  fd, gclient_output_file = tempfile.mkstemp(suffix='.json')
  os.close(fd)

  args = ['sync', '--verbose', '--reset', '--force',
         '--ignore_locks', '--output-json', gclient_output_file,
         '--nohooks', '--noprehooks', '--delete_unversioned_trees']
  if with_branch_heads:
    args += ['--with_branch_heads']
  if with_tags:
    args += ['--with_tags']
  if shallow:
    args += ['--shallow']
  if break_repo_locks:
    args += ['--break_repo_locks']
  if disable_syntax_validation:
    args += ['--disable-syntax-validation']
  for name, revision in sorted(revisions.iteritems()):
    if revision.upper() == 'HEAD':
      revision = 'origin/master'
    args.extend(['--revision', '%s@%s' % (name, revision)])

  try:
    call_gclient(*args)
  except SubprocessFailed as e:
    # Throw a GclientSyncFailed exception so we can catch this independently.
    raise GclientSyncFailed(e.message, e.code, e.output)
  else:
    with open(gclient_output_file) as f:
      return json.load(f)
  finally:
    os.remove(gclient_output_file)


def gclient_revinfo():
  return call_gclient('revinfo', '-a') or ''


def create_manifest():
  manifest = {}
  output = gclient_revinfo()
  for line in output.strip().splitlines():
    match = REVINFO_RE.match(line.strip())
    if match:
      manifest[match.group(1)] = {
        'repository': match.group(2),
        'revision': match.group(3),
      }
    else:
      print "WARNING: Couldn't match revinfo line:\n%s" % line
  return manifest


def get_commit_message_footer_map(message):
  """Returns: (dict) A dictionary of commit message footer entries.
  """
  footers = {}

  # Extract the lines in the footer block.
  lines = []
  for line in message.strip().splitlines():
    line = line.strip()
    if len(line) == 0:
      del lines[:]
      continue
    lines.append(line)

  # Parse the footer
  for line in lines:
    m = COMMIT_FOOTER_ENTRY_RE.match(line)
    if not m:
      # If any single line isn't valid, continue anyway for compatibility with
      # Gerrit (which itself uses JGit for this).
      continue
    footers[m.group(1)] = m.group(2).strip()
  return footers


def get_commit_message_footer(message, key):
  """Returns: (str/None) The footer value for 'key', or None if none was found.
  """
  return get_commit_message_footer_map(message).get(key)


# Derived from:
# http://code.activestate.com/recipes/577972-disk-usage/?in=user-4178764
def get_total_disk_space():
  cwd = os.getcwd()
  # Windows is the only platform that doesn't support os.statvfs, so
  # we need to special case this.
  if sys.platform.startswith('win'):
    _, total, free = (ctypes.c_ulonglong(), ctypes.c_ulonglong(), \
                      ctypes.c_ulonglong())
    if sys.version_info >= (3,) or isinstance(cwd, unicode):
      fn = ctypes.windll.kernel32.GetDiskFreeSpaceExW
    else:
      fn = ctypes.windll.kernel32.GetDiskFreeSpaceExA
    ret = fn(cwd, ctypes.byref(_), ctypes.byref(total), ctypes.byref(free))
    if ret == 0:
      # WinError() will fetch the last error code.
      raise ctypes.WinError()
    return (total.value, free.value)

  else:
    st = os.statvfs(cwd)
    free = st.f_bavail * st.f_frsize
    total = st.f_blocks * st.f_frsize
    return (total, free)


def get_target_revision(folder_name, git_url, revisions):
  normalized_name = folder_name.strip('/')
  if normalized_name in revisions:
    return revisions[normalized_name]
  if git_url in revisions:
    return revisions[git_url]
  return None


def force_revision(folder_name, revision):
  split_revision = revision.split(':', 1)
  branch = 'master'
  if len(split_revision) == 2:
    # Support for "branch:revision" syntax.
    branch, revision = split_revision

  if revision and revision.upper() != 'HEAD':
    git('checkout', '--force', revision, cwd=folder_name)
  else:
    # TODO(machenbach): This won't work with branch-heads, as Gerrit's
    # destination branch would be e.g. refs/branch-heads/123. But here
    # we need to pass refs/remotes/branch-heads/123 to check out.
    # This will also not work if somebody passes a local refspec like
    # refs/heads/master. It needs to translate to refs/remotes/origin/master
    # first. See also https://crbug.com/740456 .
    ref = branch if branch.startswith('refs/') else 'origin/%s' % branch
    git('checkout', '--force', ref, cwd=folder_name)


def is_broken_repo_dir(repo_dir):
  # Treat absence of 'config' as a signal of a partially deleted repo.
  return not path.exists(os.path.join(repo_dir, '.git', 'config'))


def _maybe_break_locks(checkout_path):
  """This removes all .lock files from this repo's .git directory.

  In particular, this will cleanup index.lock files, as well as ref lock
  files.
  """
  git_dir = os.path.join(checkout_path, '.git')
  for dirpath, _, filenames in os.walk(git_dir):
    for filename in filenames:
      if filename.endswith('.lock'):
        to_break = os.path.join(dirpath, filename)
        print 'breaking lock: %s' % to_break
        try:
          os.remove(to_break)
        except OSError as ex:
          print 'FAILED to break lock: %s: %s' % (to_break, ex)
          raise


def git_checkout(solutions, revisions, shallow, refs, git_cache_dir,
                 cleanup_dir):
  build_dir = os.getcwd()
  # Before we do anything, break all git_cache locks.
  if path.isdir(git_cache_dir):
    git('cache', 'unlock', '-vv', '--force', '--all',
        '--cache-dir', git_cache_dir)
    for item in os.listdir(git_cache_dir):
      filename = os.path.join(git_cache_dir, item)
      if item.endswith('.lock'):
        raise Exception('%s exists after cache unlock' % filename)
  first_solution = True
  for sln in solutions:
    # Just in case we're hitting a different git server than the one from
    # which the target revision was polled, we retry some.
    done = False
    deadline = time.time() + 60  # One minute (5 tries with exp. backoff).
    tries = 0
    while not done:
      name = sln['name']
      url = sln['url']
      if url == CHROMIUM_SRC_URL or url + '.git' == CHROMIUM_SRC_URL:
        # Experiments show there's little to be gained from
        # a shallow clone of src.
        shallow = False
      sln_dir = path.join(build_dir, name)
      s = ['--shallow'] if shallow else []
      populate_cmd = (['cache', 'populate', '--ignore_locks', '-v',
                       '--cache-dir', git_cache_dir] + s + [url])
      for ref in refs:
        populate_cmd.extend(['--ref', ref])
      git(*populate_cmd)
      mirror_dir = git(
          'cache', 'exists', '--quiet',
          '--cache-dir', git_cache_dir, url).strip()
      clone_cmd = (
          'clone', '--no-checkout', '--local', '--shared', mirror_dir, sln_dir)

      try:
        # If repo deletion was aborted midway, it may have left .git in broken
        # state.
        if path.exists(sln_dir) and is_broken_repo_dir(sln_dir):
          print 'Git repo %s appears to be broken, removing it' % sln_dir
          remove(sln_dir, cleanup_dir)

        # Use "tries=1", since we retry manually in this loop.
        if not path.isdir(sln_dir):
          git(*clone_cmd)
        else:
          git('remote', 'set-url', 'origin', mirror_dir, cwd=sln_dir)
          git('fetch', 'origin', cwd=sln_dir)
        for ref in refs:
          refspec = '%s:%s' % (ref, ref.lstrip('+'))
          git('fetch', 'origin', refspec, cwd=sln_dir)

        # Windows sometimes has trouble deleting files.
        # This can make git commands that rely on locks fail.
        # Try a few times in case Windows has trouble again (and again).
        if sys.platform.startswith('win'):
          tries = 3
          while tries:
            try:
              _maybe_break_locks(sln_dir)
              break
            except Exception:
              tries -= 1

        revision = get_target_revision(name, url, revisions) or 'HEAD'
        force_revision(sln_dir, revision)
        done = True
      except SubprocessFailed as e:
        # Exited abnormally, theres probably something wrong.
        print 'Something failed: %s.' % str(e)

        if time.time() > deadline:
          overrun = time.time() - deadline
          print 'Ran %s seconds past deadline. Aborting.' % overrun
          raise

        # Lets wipe the checkout and try again.
        tries += 1
        sleep_secs = 2**tries
        print 'waiting %s seconds and trying again...' % sleep_secs
        time.sleep(sleep_secs)
        remove(sln_dir, cleanup_dir)

    git('clean', '-dff', cwd=sln_dir)

    if first_solution:
      git_ref = git('log', '--format=%H', '--max-count=1',
                    cwd=sln_dir).strip()
    first_solution = False
  return git_ref


def _download(url):
  """Fetch url and return content, with retries for flake."""
  for attempt in xrange(ATTEMPTS):
    try:
      return urllib2.urlopen(url).read()
    except Exception:
      if attempt == ATTEMPTS - 1:
        raise


def apply_rietveld_issue(issue, patchset, root, server, _rev_map, _revision,
                         email_file, key_file, oauth2_file,
                         whitelist=None, blacklist=None):
  apply_issue_bin = ('apply_issue.bat' if sys.platform.startswith('win')
                     else 'apply_issue')
  cmd = [apply_issue_bin,
         # The patch will be applied on top of this directory.
         '--root_dir', root,
         # Tell apply_issue how to fetch the patch.
         '--issue', issue,
         '--server', server,
         # Always run apply_issue.py, otherwise it would see update.flag
         # and then bail out.
         '--force',
         # Don't run gclient sync when it sees a DEPS change.
         '--ignore_deps',
  ]
  # Use an oauth key or json file if specified.
  if oauth2_file:
    cmd.extend(['--auth-refresh-token-json', oauth2_file])
  elif email_file and key_file:
    cmd.extend(['--email-file', email_file, '--private-key-file', key_file])
  else:
    cmd.append('--no-auth')

  if patchset:
    cmd.extend(['--patchset', patchset])
  if whitelist:
    for item in whitelist:
      cmd.extend(['--whitelist', item])
  elif blacklist:
    for item in blacklist:
      cmd.extend(['--blacklist', item])

  # TODO(kjellander): Remove this hack when http://crbug.com/611808 is fixed.
  if root == path.join('src', 'third_party', 'webrtc'):
    cmd.extend(['--extra_patchlevel=1'])

  # Only try once, since subsequent failures hide the real failure.
  try:
    call(*cmd)
  except SubprocessFailed as e:
    raise PatchFailed(e.message, e.code, e.output)

def apply_gerrit_ref(gerrit_repo, gerrit_ref, root, gerrit_reset,
                     gerrit_rebase_patch_ref):
  gerrit_repo = gerrit_repo or 'origin'
  assert gerrit_ref
  base_rev = git('rev-parse', 'HEAD', cwd=root).strip()

  print '===Applying gerrit ref==='
  print 'Repo is %r @ %r, ref is %r, root is %r' % (
      gerrit_repo, base_rev, gerrit_ref, root)
  # TODO(tandrii): move the fix below to common rietveld/gerrit codepath.
  # Speculative fix: prior bot_update run with Rietveld patch may leave git
  # index with unmerged paths. bot_update calls 'checkout --force xyz' thus
  # ignoring such paths, but potentially never cleaning them up. The following
  # command will do so. See http://crbug.com/692067.
  git('reset', '--hard', cwd=root)
  try:
    git('fetch', gerrit_repo, gerrit_ref, cwd=root)
    git('checkout', 'FETCH_HEAD', cwd=root)

    if gerrit_rebase_patch_ref:
      print '===Rebasing==='
      # git rebase requires a branch to operate on.
      temp_branch_name = 'tmp/' + uuid.uuid4().hex
      try:
        ok = False
        git('checkout', '-b', temp_branch_name, cwd=root)
        try:
          git('-c', 'user.name=chrome-bot',
              '-c', 'user.email=chrome-bot@chromium.org',
              'rebase', base_rev, cwd=root)
        except SubprocessFailed:
          # Abort the rebase since there were failures.
          git('rebase', '--abort', cwd=root)
          raise

        # Get off of the temporary branch since it can't be deleted otherwise.
        cur_rev = git('rev-parse', 'HEAD', cwd=root).strip()
        git('checkout', cur_rev, cwd=root)
        git('branch', '-D', temp_branch_name, cwd=root)
        ok = True
      finally:
        if not ok:
          # Get off of the temporary branch since it can't be deleted otherwise.
          git('checkout', base_rev, cwd=root)
          git('branch', '-D', temp_branch_name, cwd=root)

    if gerrit_reset:
      git('reset', '--soft', base_rev, cwd=root)
  except SubprocessFailed as e:
    raise PatchFailed(e.message, e.code, e.output)


def get_commit_position(git_path, revision='HEAD'):
  """Dumps the 'git' log for a specific revision and parses out the commit
  position.

  If a commit position metadata key is found, its value will be returned.
  """
  # TODO(iannucci): Use git-footers for this.
  git_log = git('log', '--format=%B', '-n1', revision, cwd=git_path)
  footer_map = get_commit_message_footer_map(git_log)

  # Search for commit position metadata
  value = (footer_map.get(COMMIT_POSITION_FOOTER_KEY) or
           footer_map.get(COMMIT_ORIGINAL_POSITION_FOOTER_KEY))
  if value:
    return value
  return None


def parse_got_revision(gclient_output, got_revision_mapping):
  """Translate git gclient revision mapping to build properties."""
  properties = {}
  solutions_output = {
      # Make sure path always ends with a single slash.
      '%s/' % path.rstrip('/') : solution_output for path, solution_output
      in gclient_output['solutions'].iteritems()
  }
  for property_name, dir_name in got_revision_mapping.iteritems():
    # Make sure dir_name always ends with a single slash.
    dir_name = '%s/' % dir_name.rstrip('/')
    if dir_name not in solutions_output:
      continue
    solution_output = solutions_output[dir_name]
    if solution_output.get('scm') is None:
      # This is an ignored DEPS, so the output got_revision should be 'None'.
      revision = commit_position = None
    else:
      # Since we are using .DEPS.git, everything had better be git.
      assert solution_output.get('scm') == 'git'
      revision = git('rev-parse', 'HEAD', cwd=dir_name).strip()
      commit_position = get_commit_position(dir_name)

    properties[property_name] = revision
    if commit_position:
      properties['%s_cp' % property_name] = commit_position

  return properties


def emit_json(out_file, did_run, gclient_output=None, **kwargs):
  """Write run information into a JSON file."""
  output = {}
  output.update(gclient_output if gclient_output else {})
  output.update({'did_run': did_run})
  output.update(kwargs)
  with open(out_file, 'wb') as f:
    f.write(json.dumps(output, sort_keys=True))


def ensure_checkout(solutions, revisions, first_sln, target_os, target_os_only,
                    patch_root, issue, patchset, rietveld_server, gerrit_repo,
                    gerrit_ref, gerrit_rebase_patch_ref, revision_mapping,
                    apply_issue_email_file, apply_issue_key_file,
                    apply_issue_oauth2_file, shallow, refs, git_cache_dir,
                    cleanup_dir, gerrit_reset, disable_syntax_validation):
  # Get a checkout of each solution, without DEPS or hooks.
  # Calling git directly because there is no way to run Gclient without
  # invoking DEPS.
  print 'Fetching Git checkout'

  git_ref = git_checkout(solutions, revisions, shallow, refs, git_cache_dir,
                         cleanup_dir)

  print '===Processing patch solutions==='
  already_patched = []
  patch_root = patch_root or ''
  applied_gerrit_patch = False
  print 'Patch root is %r' % patch_root
  for solution in solutions:
    print 'Processing solution %r' % solution['name']
    if (patch_root == solution['name'] or
        solution['name'].startswith(patch_root + '/')):
      relative_root = solution['name'][len(patch_root) + 1:]
      target = '/'.join([relative_root, 'DEPS']).lstrip('/')
      print '  relative root is %r, target is %r' % (relative_root, target)
      if issue:
        apply_rietveld_issue(issue, patchset, patch_root, rietveld_server,
                             revision_mapping, git_ref, apply_issue_email_file,
                             apply_issue_key_file, apply_issue_oauth2_file,
                             whitelist=[target])
        already_patched.append(target)
      elif gerrit_ref:
        apply_gerrit_ref(gerrit_repo, gerrit_ref, patch_root, gerrit_reset,
                         gerrit_rebase_patch_ref)
        applied_gerrit_patch = True

  # Ensure our build/ directory is set up with the correct .gclient file.
  gclient_configure(solutions, target_os, target_os_only, git_cache_dir)

  # Windows sometimes has trouble deleting files. This can make git commands
  # that rely on locks fail.
  break_repo_locks = True if sys.platform.startswith('win') else False
  # We want to pass all non-solution revisions into the gclient sync call.
  solution_dirs = {sln['name'] for sln in solutions}
  gc_revisions = {
      dirname: rev for dirname, rev in revisions.iteritems()
      if dirname not in solution_dirs}
  # Gclient sometimes ignores "unmanaged": "False" in the gclient solution
  # if --revision <anything> is passed (for example, for subrepos).
  # This forces gclient to always treat solutions deps as unmanaged.
  for solution_name in list(solution_dirs):
    gc_revisions[solution_name] = 'unmanaged'
  # Let gclient do the DEPS syncing.
  # The branch-head refspec is a special case because its possible Chrome
  # src, which contains the branch-head refspecs, is DEPSed in.
  gclient_output = gclient_sync(
      BRANCH_HEADS_REFSPEC in refs,
      TAGS_REFSPEC in refs,
      shallow,
      gc_revisions,
      break_repo_locks,
      disable_syntax_validation)

  # Now that gclient_sync has finished, we should revert any .DEPS.git so that
  # presubmit doesn't complain about it being modified.
  if git('ls-files', '.DEPS.git', cwd=first_sln).strip():
    git('checkout', 'HEAD', '--', '.DEPS.git', cwd=first_sln)

  # Apply the rest of the patch here (sans DEPS)
  if issue:
    apply_rietveld_issue(issue, patchset, patch_root, rietveld_server,
                         revision_mapping, git_ref, apply_issue_email_file,
                         apply_issue_key_file, apply_issue_oauth2_file,
                         blacklist=already_patched)
  elif gerrit_ref and not applied_gerrit_patch:
    # If gerrit_ref was for solution's main repository, it has already been
    # applied above. This chunk is executed only for patches to DEPS-ed in
    # git repositories.
    apply_gerrit_ref(gerrit_repo, gerrit_ref, patch_root, gerrit_reset,
                     gerrit_rebase_patch_ref)

  # Reset the deps_file point in the solutions so that hooks get run properly.
  for sln in solutions:
    sln['deps_file'] = sln.get('deps_file', 'DEPS').replace('.DEPS.git', 'DEPS')
  gclient_configure(solutions, target_os, target_os_only, git_cache_dir)

  return gclient_output


def parse_revisions(revisions, root):
  """Turn a list of revision specs into a nice dictionary.

  We will always return a dict with {root: something}.  By default if root
  is unspecified, or if revisions is [], then revision will be assigned 'HEAD'
  """
  results = {root.strip('/'): 'HEAD'}
  expanded_revisions = []
  for revision in revisions:
    # Allow rev1,rev2,rev3 format.
    # TODO(hinoka): Delete this when webkit switches to recipes.
    expanded_revisions.extend(revision.split(','))
  for revision in expanded_revisions:
    split_revision = revision.split('@')
    if len(split_revision) == 1:
      # This is just a plain revision, set it as the revision for root.
      results[root] = split_revision[0]
    elif len(split_revision) == 2:
      # This is an alt_root@revision argument.
      current_root, current_rev = split_revision

      parsed_root = urlparse.urlparse(current_root)
      if parsed_root.scheme in ['http', 'https']:
        # We want to normalize git urls into .git urls.
        normalized_root = 'https://' + parsed_root.netloc + parsed_root.path
        if not normalized_root.endswith('.git'):
          normalized_root += '.git'
      elif parsed_root.scheme:
        print 'WARNING: Unrecognized scheme %s, ignoring' % parsed_root.scheme
        continue
      else:
        # This is probably a local path.
        normalized_root = current_root.strip('/')

      results[normalized_root] = current_rev
    else:
      print ('WARNING: %r is not recognized as a valid revision specification,'
             'skipping' % revision)
  return results


def parse_args():
  parse = optparse.OptionParser()

  parse.add_option('--issue', help='Issue number to patch from.')
  parse.add_option('--patchset',
                   help='Patchset from issue to patch from, if applicable.')
  parse.add_option('--apply_issue_email_file',
                   help='--email-file option passthrough for apply_patch.py.')
  parse.add_option('--apply_issue_key_file',
                   help='--private-key-file option passthrough for '
                        'apply_patch.py.')
  parse.add_option('--apply_issue_oauth2_file',
                   help='--auth-refresh-token-json option passthrough for '
                        'apply_patch.py.')
  parse.add_option('--root', dest='patch_root',
                   help='DEPRECATED: Use --patch_root.')
  parse.add_option('--patch_root', help='Directory to patch on top of.')
  parse.add_option('--rietveld_server',
                   default='codereview.chromium.org',
                   help='Rietveld server.')
  parse.add_option('--gerrit_repo',
                   help='Gerrit repository to pull the ref from.')
  parse.add_option('--gerrit_ref', help='Gerrit ref to apply.')
  parse.add_option('--gerrit_no_rebase_patch_ref', action='store_true',
                   help='Bypass rebase of Gerrit patch ref after checkout.')
  parse.add_option('--gerrit_no_reset', action='store_true',
                   help='Bypass calling reset after applying a gerrit ref.')
  parse.add_option('--specs', help='Gcilent spec.')
  parse.add_option('--spec-path', help='Path to a Gcilent spec file.')
  parse.add_option('--revision_mapping_file',
                   help=('Path to a json file of the form '
                         '{"property_name": "path/to/repo/"}'))
  parse.add_option('--revision', action='append', default=[],
                   help='Revision to check out. Can be any form of git ref. '
                        'Can prepend root@<rev> to specify which repository, '
                        'where root is either a filesystem path or git https '
                        'url. To specify Tip of Tree, set rev to HEAD. ')
  # TODO(machenbach): Remove the flag when all uses have been removed.
  parse.add_option('--output_manifest', action='store_true',
                   help=('Deprecated.'))
  parse.add_option('--clobber', action='store_true',
                   help='Delete checkout first, always')
  parse.add_option('--output_json',
                   help='Output JSON information into a specified file')
  parse.add_option('--no_shallow', action='store_true',
                   help='Bypass disk detection and never shallow clone. '
                        'Does not override the --shallow flag')
  parse.add_option('--refs', action='append',
                   help='Also fetch this refspec for the main solution(s). '
                        'Eg. +refs/branch-heads/*')
  parse.add_option('--with_branch_heads', action='store_true',
                    help='Always pass --with_branch_heads to gclient.  This '
                          'does the same thing as --refs +refs/branch-heads/*')
  parse.add_option('--with_tags', action='store_true',
                    help='Always pass --with_tags to gclient.  This '
                          'does the same thing as --refs +refs/tags/*')
  parse.add_option('--git-cache-dir', help='Path to git cache directory.')
  parse.add_option('--cleanup-dir',
                   help='Path to a cleanup directory that can be used for '
                        'deferred file cleanup.')
  parse.add_option(
      '--disable-syntax-validation', action='store_true',
      help='Disable validation of .gclient and DEPS syntax.')

  options, args = parse.parse_args()

  if options.spec_path:
    if options.specs:
      parse.error('At most one of --spec-path and --specs may be specified.')
    with open(options.spec_path, 'r') as fd:
      options.specs = fd.read()

  if not options.git_cache_dir:
    parse.error('--git-cache-dir is required')

  if not options.refs:
    options.refs = []

  if options.with_branch_heads:
    options.refs.append(BRANCH_HEADS_REFSPEC)
    del options.with_branch_heads

  if options.with_tags:
    options.refs.append(TAGS_REFSPEC)
    del options.with_tags

  try:
    if not options.revision_mapping_file:
      parse.error('--revision_mapping_file is required')

    with open(options.revision_mapping_file, 'r') as f:
      options.revision_mapping = json.load(f)
  except Exception as e:
    print (
        'WARNING: Caught execption while parsing revision_mapping*: %s'
        % (str(e),)
    )

  # Because we print CACHE_DIR out into a .gclient file, and then later run
  # eval() on it, backslashes need to be escaped, otherwise "E:\b\build" gets
  # parsed as "E:[\x08][\x08]uild".
  if sys.platform.startswith('win'):
    options.git_cache_dir = options.git_cache_dir.replace('\\', '\\\\')

  return options, args


def prepare(options, git_slns, active):
  """Prepares the target folder before we checkout."""
  dir_names = [sln.get('name') for sln in git_slns if 'name' in sln]
  if options.clobber:
    ensure_no_checkout(dir_names, options.cleanup_dir)
  # Make sure we tell recipes that we didn't run if the script exits here.
  emit_json(options.output_json, did_run=active)

  # Do a shallow checkout if the disk is less than 100GB.
  total_disk_space, free_disk_space = get_total_disk_space()
  total_disk_space_gb = int(total_disk_space / (1024 * 1024 * 1024))
  used_disk_space_gb = int((total_disk_space - free_disk_space)
                           / (1024 * 1024 * 1024))
  percent_used = int(used_disk_space_gb * 100 / total_disk_space_gb)
  step_text = '[%dGB/%dGB used (%d%%)]' % (used_disk_space_gb,
                                           total_disk_space_gb,
                                           percent_used)
  if not options.output_json:
    print '@@@STEP_TEXT@%s@@@' % step_text
  shallow = (total_disk_space < SHALLOW_CLONE_THRESHOLD
             and not options.no_shallow)

  # The first solution is where the primary DEPS file resides.
  first_sln = dir_names[0]

  # Split all the revision specifications into a nice dict.
  print 'Revisions: %s' % options.revision
  revisions = parse_revisions(options.revision, first_sln)
  print 'Fetching Git checkout at %s@%s' % (first_sln, revisions[first_sln])
  return revisions, step_text, shallow


def checkout(options, git_slns, specs, revisions, step_text, shallow):
  print 'Checking git version...'
  ver = git('version').strip()
  print 'Using %s' % ver

  first_sln = git_slns[0]['name']
  dir_names = [sln.get('name') for sln in git_slns if 'name' in sln]
  try:
    # Outer try is for catching patch failures and exiting gracefully.
    # Inner try is for catching gclient failures and retrying gracefully.
    try:
      checkout_parameters = dict(
          # First, pass in the base of what we want to check out.
          solutions=git_slns,
          revisions=revisions,
          first_sln=first_sln,

          # Also, target os variables for gclient.
          target_os=specs.get('target_os', []),
          target_os_only=specs.get('target_os_only', False),

          # Then, pass in information about how to patch.
          patch_root=options.patch_root,
          issue=options.issue,
          patchset=options.patchset,
          rietveld_server=options.rietveld_server,
          gerrit_repo=options.gerrit_repo,
          gerrit_ref=options.gerrit_ref,
          gerrit_rebase_patch_ref=not options.gerrit_no_rebase_patch_ref,
          revision_mapping=options.revision_mapping,
          apply_issue_email_file=options.apply_issue_email_file,
          apply_issue_key_file=options.apply_issue_key_file,
          apply_issue_oauth2_file=options.apply_issue_oauth2_file,

          # Finally, extra configurations such as shallowness of the clone.
          shallow=shallow,
          refs=options.refs,
          git_cache_dir=options.git_cache_dir,
          cleanup_dir=options.cleanup_dir,
          gerrit_reset=not options.gerrit_no_reset,
          disable_syntax_validation=options.disable_syntax_validation)
      gclient_output = ensure_checkout(**checkout_parameters)
    except GclientSyncFailed:
      print 'We failed gclient sync, lets delete the checkout and retry.'
      ensure_no_checkout(dir_names, options.cleanup_dir)
      gclient_output = ensure_checkout(**checkout_parameters)
  except PatchFailed as e:
    if options.output_json:
      # Tell recipes information such as root, got_revision, etc.
      emit_json(options.output_json,
                did_run=True,
                root=first_sln,
                patch_apply_return_code=e.code,
                patch_root=options.patch_root,
                patch_failure=True,
                failed_patch_body=e.output,
                step_text='%s PATCH FAILED' % step_text,
                fixed_revisions=revisions)
    raise

  # Take care of got_revisions outputs.
  revision_mapping = GOT_REVISION_MAPPINGS.get(git_slns[0]['url'], {})
  if options.revision_mapping:
    revision_mapping.update(options.revision_mapping)

  # If the repo is not in the default GOT_REVISION_MAPPINGS and no
  # revision_mapping were specified on the command line then
  # default to setting 'got_revision' based on the first solution.
  if not revision_mapping:
    revision_mapping['got_revision'] = first_sln

  got_revisions = parse_got_revision(gclient_output, revision_mapping)

  if not got_revisions:
    # TODO(hinoka): We should probably bail out here, but in the interest
    # of giving mis-configured bots some time to get fixed use a dummy
    # revision here.
    got_revisions = { 'got_revision': 'BOT_UPDATE_NO_REV_FOUND' }
    #raise Exception('No got_revision(s) found in gclient output')

  if options.output_json:
    # Tell recipes information such as root, got_revision, etc.
    emit_json(options.output_json,
              did_run=True,
              root=first_sln,
              patch_root=options.patch_root,
              step_text=step_text,
              fixed_revisions=revisions,
              properties=got_revisions,
              manifest=create_manifest())


def print_debug_info():
  print "Debugging info:"
  debug_params = {
    'CURRENT_DIR': path.abspath(os.getcwd()),
    'THIS_DIR': THIS_DIR,
    'DEPOT_TOOLS_DIR': DEPOT_TOOLS_DIR,
  }
  for k, v in sorted(debug_params.iteritems()):
    print "%s: %r" % (k, v)


def main():
  # Get inputs.
  options, _ = parse_args()

  # Check if this script should activate or not.
  active = True

  # Print a helpful message to tell developers whats going on with this step.
  print_debug_info()

  # Parse, manipulate, and print the gclient solutions.
  specs = {}
  exec(options.specs, specs)
  orig_solutions = specs.get('solutions', [])
  git_slns = modify_solutions(orig_solutions)

  solutions_printer(git_slns)

  # Creating hardlinks during a build can interact with git reset in
  # unfortunate ways if git's index isn't refreshed beforehand. (See
  # crbug.com/330461#c13 for an explanation.)
  try:
    call_gclient('recurse', '-v', 'git', 'update-index', '--refresh')
  except SubprocessFailed:
    # Failure here (and nowhere else) may have adverse effects on the
    # compile time of the build but shouldn't affect its ability to
    # successfully complete.
    print 'WARNING: Failed to update git indices.'

  try:
    # Dun dun dun, the main part of bot_update.
    revisions, step_text, shallow = prepare(options, git_slns, active)
    checkout(options, git_slns, specs, revisions, step_text, shallow)

  except PatchFailed as e:
    # Return a specific non-zero exit code for patch failure (because it is
    # a failure), but make it different than other failures to distinguish
    # between infra failures (independent from patch author), and patch
    # failures (that patch author can fix). However, PatchFailure due to
    # download patch failure is still an infra problem.
    if e.code == 3:
      # Patch download problem.
      return 87
    # Genuine patch problem.
    return 88


if __name__ == '__main__':
  sys.exit(main())
