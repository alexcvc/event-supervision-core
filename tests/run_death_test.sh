#!/usr/bin/env bash
# Runs a "death test" binary and reports success iff it was killed by a signal
# (e.g. SIGABRT from std::terminate()). CTest treats a subprocess it observes
# dying to a signal as an unconditional "Exception" failure, ignoring
# WILL_FAIL/PASS_REGULAR_EXPRESSION — so this wrapper absorbs the signal
# itself and turns the outcome into a normal (non-signal) exit code that
# CTest can evaluate as pass/fail.
set -u

"$@"
status=$?

if [ "$status" -gt 128 ]; then
  echo "death test binary terminated via signal $((status - 128)) as expected"
  exit 0
fi

echo "death test binary exited with status $status instead of being killed by a signal"
exit 1
