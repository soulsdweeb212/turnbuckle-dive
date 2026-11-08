# Continuous integration (CI)

Every time anyone pushes to GitHub (any branch) or opens a pull request, GitHub builds Turnbuckle Dive on two machines
it lends us, one running Ubuntu Linux and one running Windows. On each it runs the tests and opens hello's window to
check its picture. This is called continuous integration, or CI. It catches mistakes that only show up on the other
system, and it proves that the code in the repository builds from nothing, without anything that happens to be
installed on your own computer. It is free for public repositories.

Everything CI does is in [.github/workflows/ci.yml](../.github/workflows/ci.yml), and every step there has a comment.
CI uses the same CMake presets as a build on your own computer (see [CMakePresets.json](../CMakePresets.json)), so the
commands it runs are the commands you would type.

## Reading the results

- The badge at the top of the README says "passing" (green) or "failing" (red) for the latest run on `main`.
- On GitHub, the **Actions** tab lists every run. Next to each commit, a green tick means both jobs passed, a red cross
  means at least one failed, and a yellow dot means it is still running.
- Click a run, then a job ("Linux (GCC)" or "Windows (MSVC)"), then a step to see what it printed. The failed step is
  marked with a red cross, and its last lines usually say what went wrong.
- **Re-run jobs** (top right of a run) runs it again, for example after a download failed.
- **Run workflow** (on the Actions tab, after choosing "CI" on the left) starts a run by hand.
- From a terminal, GitHub's `gh` tool shows the same: `gh run list`, `gh run watch`, and `gh run view --log-failed`
  for the output of the failed steps. Install it with `sudo apt install gh` and log in once with `gh auth login`.

A Linux run takes about 10 minutes. The first Windows run takes about 40, because vcpkg builds SDL3 and the other
libraries from source; later runs reuse what it built and take about 15.

## What each job does

The steps below have the same names as on GitHub.

**Linux (GCC)**, on Ubuntu 24.04:

1. *Get the code, with the LibUltraShip submodule*: the same as `git clone --recursive`.
2. *Install the tools and libraries*: the `apt` list from the README, plus `xvfb` (a virtual screen) and
   `libgl1-mesa-dri` (Mesa's software renderer), so hello can open a window on a machine without a screen.
3. *Configure (preset "debug")*: `cmake --preset debug`. This downloads SDL3, ImGui and LibUltraShip's small libraries,
   and applies the fixes in `cmake/patches/libultraship` to LibUltraShip.
4. *Build*: `cmake --build --preset debug`.
5. *Run the tests*: `ctest --preset debug`. The tests are `sha1` (known SHA-1 answers) and `rom_missing` (the program
   starts with a ROM path that does not exist and must say "file not found"). No test uses a ROM; CI must never have
   one.
6. *Check hello's picture*: in `build/debug`, `xvfb-run ./hello --check --frames 30`. hello draws 30 frames; after the
   third it reads the picture back and checks the white markers in its corners. It exits with code 2 if they are
   wrong, which fails the step.

**Windows (MSVC)**, on Windows Server 2022 with Visual Studio 2022:

1. *Get the code, with the LibUltraShip submodule*.
2. *Restore the libraries vcpkg built in earlier runs*: on Windows, LibUltraShip gets SDL3, GLEW, zlib, bzip2, libzip,
   nlohmann-json, tinyxml2 and spdlog through vcpkg, which builds them from source and keeps a zip of each. This step
   brings back the zips saved by an earlier run (a GitHub "cache"), so vcpkg can skip those builds.
3. *Set up Visual Studio's compiler (cl) for 64-bit Windows*: what the "Developer PowerShell for VS 2022" does.
4. *Configure (preset "windows-relwithdebinfo")*: LibUltraShip's CMake downloads vcpkg into `build/_vcpkg` and has it
   build (or unzip) the libraries, then everything else happens as on Linux.
5. *Save the libraries vcpkg built*: stores the zips for the next run. It runs even if a later step fails.
6. *Build*, 7. *Run the tests*, 8. *Check hello's picture*: as on Linux. On Windows, hello draws with Direct3D 11.

Windows uses RelWithDebInfo rather than Debug because in a Debug build LibUltraShip moves the program's output to a
console window of its own, and CI would not see what hello prints.

## Running the same steps on your own computer

**Linux or WSL**, from the repository folder (the first line only once, in a fresh clone):

```
git submodule update --init
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cd build/debug && ./hello --check --frames 30
```

Under WSL, hello's window opens on your desktop, so no virtual screen is needed.

**Windows**: install Visual Studio 2022 with the "Desktop development with C++" workload, and Git for Windows. Open
"Developer PowerShell for VS 2022", go to the repository folder, and run:

```
git submodule update --init
cmake --preset windows-relwithdebinfo
cmake --build --preset windows-relwithdebinfo
ctest --preset windows-relwithdebinfo
cd build\windows-relwithdebinfo; .\hello.exe --check --frames 30
```

The first configure downloads vcpkg into `build\_vcpkg` and builds the libraries, which takes 15 to 30 minutes. Deleting
the `build` folder deletes that too.

## When a job fails

| Failed step | What it usually means | What to do |
|---|---|---|
| Install the tools and libraries | Ubuntu's package servers had a hiccup, or a package was renamed | Re-run. If it keeps failing, find the new name with `apt-cache search` and change it in both README.md and ci.yml. |
| Configure, with a download error | A download failed (SDL3, ImGui, vcpkg or a library) | Re-run. |
| Configure, with "Could not apply ... to lib/libultraship" | LibUltraShip was moved to a newer commit and one of our fixes no longer fits | Read the top of that `.patch` file: it says what it fixes. If LibUltraShip fixed it, delete the patch; if not, make it again against the new code. |
| Configure, on Windows only, with vcpkg errors | vcpkg changed one of the libraries | Re-run. If it persists, delete the caches (Actions tab, then Caches) and re-run. LibUltraShip's list of libraries is in `lib/libultraship/cmake/dependencies/windows-vcpkg.cmake`. |
| Build | A compile error; the output names the file and line | Fix the code. If only Windows fails, Visual Studio's compiler is stricter or different there; the Linux build will not show it. |
| Run the tests | A test failed; ctest shows its output | Run `ctest --preset debug` yourself; add `-V` to see everything the tests print. |
| Check hello's picture, exit code 2 | hello ran, but the corner check printed "check: FAIL", or hello stopped before the third frame ("check: not run") | Run `./hello --check` yourself. A recent change to the drawing code or to LibUltraShip is the likely cause. |
| Check hello's picture, exit code 1 | hello could not start; the line starting with "hello:" says why | Fix what it names (for example a missing `f3d.o2r`). |
| Any step, "cancelled" | A newer push to the same branch replaced this run | Nothing; the newer run is the one that counts. |

## Keeping CI working

- The list of `apt` packages is in two places, README.md and ci.yml. Change both together.
- The machines are named in ci.yml (`ubuntu-24.04`, `windows-2022`). GitHub retires old ones after a few years and
  announces it in advance; then change the name (for example to `windows-2025`) and check that a run still passes.
- The ready-made steps CI uses are `actions/checkout`, `actions/cache` and `ilammy/msvc-dev-cmd`, each with a version
  (`@v7`, `@v6`, `@v1`). Raise a version when GitHub warns that it is outdated.
- LibUltraShip's CMake updates vcpkg to its newest version on every Windows configure, so the library versions on
  Windows can change with no change in this repository. If a Windows run suddenly fails in Configure or Build for no
  reason you can see, suspect this first.
- The saved vcpkg zips are listed under Actions, then Caches. Deleting them is always safe: the next run builds the
  libraries again.
- If the repository ever becomes private, CI time counts against GitHub's free monthly minutes, and Windows minutes
  count double.
