#!/usr/bin/env python
# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Runs a script that can run as an isolate (or not).

The main requirement is that

  --isolated-script-test-output=[FILENAME]

is passed on the command line to run_isolated_script_tests. This gets
remapped to the command line argument --write-full-results-to.

json is written to that file in the format produced by
common.parse_common_test_results.

Optional argument:

  --isolated-script-test-filter=[TEST_NAMES]

is a double-colon-separated ("::") list of test names, to run just that subset
of tests. This list is parsed by this harness and sent down via the --test-list
argument.

This script is intended to be the base command invoked by the isolate,
followed by a subsequent Python script. It could be generalized to
invoke an arbitrary executable.
"""

# TODO(tansell): Remove this script once LayoutTests can accept the isolated
# arguments and start xvfb itself.

import argparse
import json
import os
import pprint
import sys
import tempfile


import common

# Add src/testing/ into sys.path for importing xvfb.
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import xvfb


# Known typ test runners this script wraps. They need a different argument name
# when selecting which tests to run.
# TODO(dpranke): Detect if the wrapped test suite uses typ better.
KNOWN_TYP_TEST_RUNNERS = {'run_blinkpy_tests.py', 'metrics_python_tests.py'}


class TypUnittestAdapter(common.BaseIsolatedScriptArgsAdapter):

  def __init__(self):
    super(TypUnittestAdapter, self).__init__()
    self._temp_filter_file = None

  def generate_sharding_args(self, total_shards, shard_index):
    # This script only uses environment variable for sharding.
    del total_shards, shard_index  # unused
    return []

  def generate_test_output_args(self, output):
    return ['--write-full-results-to', output]

  def generate_test_filter_args(self, test_filter_str):
    filter_list = common.extract_filter_list(test_filter_str)
    self._temp_filter_file = tempfile.NamedTemporaryFile(
        mode='w', delete=False)
    self._temp_filter_file.write('\n'.join(filter_list))
    self._temp_filter_file.close()
    arg_name = 'test-list'
    if KNOWN_TYP_TEST_RUNNERS.intersection(self.rest_args):
      arg_name = 'file-list'

    return ['--%s=' % arg_name + self._temp_filter_file.name]

  def run_test(self):
    try:
      return super(TypUnittestAdapter, self).run_test()
    finally:
      # Clean up the temp filter file at the end after the test has
      # finished the run if needed.
      if self._temp_filter_file:
        os.unlink(self._temp_filter_file.name)

def main():
  adapter = TypUnittestAdapter()
  adapter.run_test()

# This is not really a "script test" so does not need to manually add
# any additional compile targets.
def main_compile_targets(args):
  json.dump([], args.output)


if __name__ == '__main__':
  # Conform minimally to the protocol defined by ScriptTest.
  if 'compile_targets' in sys.argv:
    funcs = {
      'run': None,
      'compile_targets': main_compile_targets,
    }
    sys.exit(common.run_script(sys.argv[1:], funcs))
  sys.exit(main())
