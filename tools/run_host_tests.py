#!/usr/bin/env python3
"""
run_host_tests.py - compiles and runs the PC-side C++ tests in
common/host_test/ with PlatformIO's MinGW toolchain
(pio pkg install -g -t platformio/toolchain-gccmingw32).

Exit code 0 when every test binary passes.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TEST_DIR = ROOT / "common" / "host_test"
GPP = Path.home() / ".platformio" / "packages" / "toolchain-gccmingw32" / "bin" / "g++.exe"


def main():
    if not GPP.is_file():
        print(f"g++ not found at {GPP}\nInstall: pio pkg install -g -t platformio/toolchain-gccmingw32")
        sys.exit(2)
    env = dict(os.environ, PATH=str(GPP.parent) + os.pathsep + os.environ.get("PATH", ""))
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for src in sorted(TEST_DIR.glob("test_*.cpp")):
            exe = Path(tmp) / (src.stem + ".exe")
            build = subprocess.run([str(GPP), "-std=c++11", "-Wall", "-Wextra", "-O1", "-I", str(TEST_DIR),
                                    str(src), "-o", str(exe)], capture_output=True, text=True, env=env)
            if build.returncode:
                print(f"BUILD FAILED {src.name}\n{build.stdout}{build.stderr}")
                failed += 1
                continue
            if build.stderr.strip():
                print(f"warnings in {src.name}:\n{build.stderr}")
                failed += 1
            run = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
            print(run.stdout.rstrip())
            failed += run.returncode != 0
    print(f"\n{'ALL HOST TESTS PASSED' if not failed else f'{failed} HOST TEST FILE(S) FAILED'}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
