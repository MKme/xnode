"""Use SCons' GCC response files for long Windows compile commands.

T-Deck Pro's bundled drivers and ESP32-S3 SDK include paths can exceed the
Windows CreateProcess command-line limit in an isolated checkout. This keeps
the same compiler, flags and source paths; Unix builds retain their defaults.
"""
import os
Import("env")
if os.name == "nt":
    env["MAXLINELENGTH"] = 8000
    for key in ("CCCOM", "CXXCOM"):
        action = env.get(key)
        if isinstance(action, str) and "TEMPFILE(" not in action:
            env[key] = "${TEMPFILE(" + repr(action) + ")}"
    print("Windows long compiler commands use SCons response files (same toolchain/flags).")
