"""Linux-only host compatibility for the pinned native simulator dependencies."""
from pathlib import Path
import sys

Import("env")  # noqa: F821 (PlatformIO/SCons)

if sys.platform != "linux":
    Return()  # noqa: F821 (SCons)

simulator = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "simulator/src"
# The pinned GIF decoder uses Arduino timing on Linux without including Arduino.h.
# Supply only that translation unit with the simulator's timing declarations.
def animated_gif_with_arduino(node_env, node):
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] +
                           ["-include", str(simulator / "Arduino.h")])


env.AddBuildMiddleware(animated_gif_with_arduino, "*AnimatedGIF.cpp")

# The simulator's Linux MD5 wrapper needs libcrypto after the static libraries.
env.Append(LIBS=["crypto"])

# Both the pinned firmware and simulator provide uzlib checksum implementations.
# Keep the firmware definitions and give only the simulator fallback definitions
# private names; retain its still-needed serial printf fallback. Do not relax the
# linker's duplicate-symbol checks for the rest of the build.
def private_checksum_fallbacks(node_env, node):
    return node_env.Object(node, CCFLAGS=node_env["CCFLAGS"] + [
        "-Duzlib_adler32=booksync_sim_fallback_adler32",
        "-Duzlib_crc32=booksync_sim_fallback_crc32",
    ])


env.AddBuildMiddleware(private_checksum_fallbacks, "*firmware_link_stubs.cpp")
