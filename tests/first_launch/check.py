"""first_launch: checks the player's launch (src/first_launch.h) with a folder of its own for the port's files.

1. The first launch, with an empty folder: the program finds the ROM, writes the game's archive into the folder, and
   runs from the archive.
2. The next launch: it runs from the archive and writes nothing.
3. A damaged archive (cut to half its size): the program says it cannot be used, writes it again, and runs from it.
At the end the folder is deleted, archive and all: the archive holds the game's data and is never kept.

The folder is given to the program in $SHIP_HOME, which LibUltraShip reads on Linux only, so on other systems the test
is skipped; it is skipped too (exit code 77) when the program finds no ROM, as on CI.

Usage: check.py PROGRAM FOLDER
"""
import os
import shutil
import subprocess
import sys

SKIP = 77
FROM_ARCHIVE = "Status: supported ROM, from the game's archive"
WRITING = "Writing the game's archive"


def run(program, folder):
    env = dict(os.environ, SHIP_HOME=folder)
    env.pop("TD_ROM", None)  # a ROM given would make it the launch with a ROM, not the search
    done = subprocess.run([program], cwd=folder, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    # The program's output goes into the test's, where CTest finds any sanitizer report (the asan preset).
    print(done.stdout)
    return done.returncode, done.stdout


def check(step, ok):
    if not ok:
        print(f"FAIL: {step}")
        sys.exit(1)


def main():
    if len(sys.argv) != 3:
        print("usage: check.py PROGRAM FOLDER")
        return 2
    program, folder = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    if not sys.platform.startswith("linux"):
        print("SKIP: LibUltraShip reads SHIP_HOME on Linux only")
        return SKIP
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    archive = os.path.join(folder, "revenge.o2r")
    try:
        code, out = run(program, folder)
        if "No ROM found" in out:
            print("SKIP: no ROM")
            return SKIP
        check("the first launch writes the archive and runs from it",
              code == 0 and WRITING in out and FROM_ARCHIVE in out and os.path.isfile(archive))

        code, out = run(program, folder)
        check("the next launch runs from the archive and writes nothing",
              code == 0 and WRITING not in out and FROM_ARCHIVE in out)

        with open(archive, "r+b") as f:
            f.truncate(os.path.getsize(archive) // 2)
        code, out = run(program, folder)
        check("a damaged archive is written again",
              code == 0 and "cannot be used" in out and WRITING in out and FROM_ARCHIVE in out)
    finally:
        shutil.rmtree(folder, ignore_errors=True)
    print("PASS: the first launch writes the game's archive, the next starts from it, and a damaged one is replaced")
    return 0


if __name__ == "__main__":
    sys.exit(main())
