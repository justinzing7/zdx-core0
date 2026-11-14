# PlatformIO pre-script: injects -DGIT_SHA="<short sha>[+]" into the build.
# "+" suffix = uncommitted changes. Falls back to "nogit".
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def _git(*args):
    return subprocess.check_output(["git", *args], stderr=subprocess.DEVNULL).decode().strip()


try:
    sha = _git("rev-parse", "--short", "HEAD")
    if _git("status", "--porcelain"):
        sha += "+"
except Exception:
    sha = "nogit"

env.Append(CPPDEFINES=[("GIT_SHA", '\\"%s\\"' % sha)])  # noqa: F821
