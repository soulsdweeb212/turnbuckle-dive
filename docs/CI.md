# Continuous integration (CI)

Every time anyone pushes to GitHub (any branch) or opens a pull request, GitHub builds Turnbuckle Dive on two machines
it lends us, one running Ubuntu Linux and one running Windows. On each it runs the tests and opens hello's window to
check its picture. This is called continuous integration, or CI. It catches mistakes that only show up on the other
system, and it proves that the code in the repository builds from nothing, without anything that happens to be
installed on your own computer.

Everything CI does is in [.github/workflows/ci.yml](../.github/workflows/ci.yml), and every step there has a comment.
CI uses the same CMake presets as a build on your own computer (see [CMakePresets.json](../CMakePresets.json)), so the
commands it runs are the commands you would type.

## How it works

GitHub's service for this is called **GitHub Actions**. On every push it takes a clean computer, follows a written
recipe to build and test the project, and puts a green tick or a red cross next to the commit.

**The recipe file.** GitHub looks in the folder `.github/workflows/` of the repository. Each `.yml` file there is one
recipe, which GitHub calls a *workflow*. Ours is `.github/workflows/ci.yml`. A `.yml` (YAML) file is plain text laid
out like an outline: what is indented under a line belongs to that line. The indentation must be spaces, never tabs.

**When it runs.** The `on:` part of the file lists what starts a run. Ours has `push` (any push to any branch),
`pull_request` (someone proposes a change), and `workflow_dispatch` (a "Run workflow" button on GitHub).

**Where it runs.** For each job, GitHub starts a brand-new virtual computer, called a *runner*, named by `runs-on:`
(ours are `ubuntu-24.04` and `windows-2022`). It comes with common tools such as git, CMake and compilers, but nothing
of ours, and it is thrown away when the job ends. So if the project builds there, it does not depend on anything that
happens to be installed on one person's computer.

**Jobs and steps.** The file has two *jobs*, `linux` and `windows`. They run at the same time, each on its own runner.
A job is a list of *steps* that run in order, top to bottom. A step is one of two kinds:

- `run:` types a command into the runner's terminal (bash on Linux, PowerShell on Windows), for example
  `cmake --preset debug`.
- `uses:` runs a ready-made step that someone published on GitHub, called an *action*. Ours use
  `actions/checkout` (gets our code), `actions/cache` (keeps files between runs) and `ilammy/msvc-dev-cmd` (sets up
  Visual Studio's compiler). The text after `@` is the action's version, for example `@v7`.

**Pass or fail.** Every command ends with an *exit code*: 0 means it worked, anything else means it did not. When a
step's command ends with a non-zero code, the step fails, the job stops there, and the job is marked with a red cross.
That is why our checks end with a non-zero code when something is wrong: `ctest` does when a test fails, and
`hello --check` ends with code 2 when the picture is wrong and 1 when hello's own start-up checks fail. A crash fails
the step at once, and a hang is stopped by the step's time limit of 2 minutes (on Linux this shows as exit code 124).
On Windows the steps run in PowerShell, which reports only the exit code of a step's last command, so there every
command has a step of its own.

**What happens after a push:**

1. You push a commit (from VS Code, or with `git push`).
2. GitHub reads `.github/workflows/ci.yml` and sees that `push` is one of its triggers.
3. It starts two fresh runners, one Linux and one Windows.
4. On each, the steps run in order: get the code, install the tools, configure, build, run the tests, check hello's
   picture.
5. Each job ends green or red. The commit gets a green tick only if both are green, and the README badge shows the
   result of the latest run on `main`.

**The cache.** Because runners are thrown away, anything built in one run is lost. The Windows job therefore saves
vcpkg's built libraries in GitHub's storage right after the Configure step and restores them at the start of the next
run, so they are not built every time.

**Safety.** The workflow needs no passwords or keys, and `permissions: contents: read` means it can only read the code.
No ROM or file from the game may ever be in the repository, so none can reach CI.

## Reading the results

- The badge at the top of the README says "passing" (green) or "failing" (red) for the latest run on `main`.
- On GitHub, the **Actions** tab lists every run. Click a run, then a job ("Linux (GCC)" or "Windows (MSVC)"), then a
  step to see what it printed.
- **Run workflow** (on the Actions tab, after choosing "CI" on the left) starts a run by hand.
- From a terminal, GitHub's `gh` tool (`sudo apt install gh`, then `gh auth login` once) shows the same:
  `gh run list` lists the latest runs with their numbers, `gh run watch` follows one live, and
  `gh run view NUMBER --log-failed` prints what its failed steps printed.

## What each job does

The steps below have the same names as on GitHub.

**Linux (GCC)**, on Ubuntu 24.04:

1. *Get the code, with the LibUltraShip submodule*: the same as `git clone --recursive`.
2. *Install the tools and libraries*: the `apt` list from the README, plus `xvfb` (a virtual screen) and
   `libgl1-mesa-dri` (Mesa's software renderer), so hello can open a window on a machine without a screen.
3. *Configure (preset "debug")*: `cmake --preset debug`. This downloads SDL3, ImGui and LibUltraShip's small libraries,
   and applies the fixes in `cmake/patches/libultraship` to LibUltraShip. CI has no copy of the decompilation, so
   configure prints `Game code: not built (...)` and the build leaves the game's code out
   ([GAME_CODE.md](GAME_CODE.md)); `turnbuckle-dive` then only checks the ROM.
4. *Build*: `cmake --build --preset debug`.
5. *Run the tests*: `ctest --preset debug`. The tests are `sha1` (known SHA-1 answers) and `rom_missing` (the program
   starts with a ROM path that does not exist and must say "file not found"). No test on CI uses a ROM; CI must never
   have one. The tests that do (`rom_loader`, `rom_loader_detects_change`, and the asset viewer's `viewer_reference`
   and `viewer_check`) exist only in builds with the game's code, so CI does not build them, nor the viewer itself.
6. *Check hello's picture*: in `build/debug`, `xvfb-run ./hello --check --frames 30`. hello draws 30 frames; after the
   third it reads the picture back and checks the white markers in its corners, the shapes placed with
   `gSPForceMatrix` and `gSPModifyVertex`, and the colours of eight squares: textures read through IA16 and RGBA16
   palettes, and textures tagged IA4 and IA8 drawn with palettes on and off (the comment at the top of
   `hello/main.cpp` describes the picture).

**Windows (MSVC)**, on Windows Server 2022 with Visual Studio 2022:

1. *Get the code, with the LibUltraShip submodule*.
2. *Restore the libraries vcpkg built in earlier runs*: on Windows, LibUltraShip gets SDL3, GLEW, zlib, bzip2, libzip,
   nlohmann-json, tinyxml2 and spdlog through vcpkg, which builds them from source and keeps a zip of each. This step
   brings back the zips saved by an earlier run, so vcpkg can skip those builds.
3. *Set up Visual Studio's compiler (cl) for 64-bit Windows*: what the "Developer PowerShell for VS 2022" does.
4. *Configure (preset "windows-relwithdebinfo")*: LibUltraShip's CMake downloads vcpkg into `build/_vcpkg` and has it
   build (or unzip) the libraries, then everything else happens as on Linux.
5. *Save the libraries vcpkg built*: stores the zips for the next run, even if a later step fails.
6. *Build*, 7. *Run the tests*, 8. *Check hello's picture*: as on Linux. On Windows, hello draws with Direct3D 11.
9. *Show LibUltraShip's log*, only when a step failed. On Windows, LibUltraShip writes its messages to
   `logs/hello.log` next to hello rather than to the step's output, so this step prints that file.

The Windows build also leaves the game's code out, because the game's C needs GCC or Clang (configure says so).
Windows uses RelWithDebInfo rather than Debug because in a Debug build LibUltraShip moves the program's output to a
console window of its own, and CI would not see what hello prints. Some of LibUltraShip's errors on Windows open a
message box; on CI nobody can close it, so such a run ends at the step's time limit.

## Running the same steps on your own computer

**Linux or WSL**, from the repository folder (the first line only once, in a fresh clone):

```
git submodule update --init
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
cd build/debug && ./hello --check --frames 30
```

Under WSL, hello's window opens on your desktop, so no virtual screen is needed. With the decompilation (its branch
`port-groundwork`) in the folder next to the repository, your build also includes the game's code, and `ctest` also runs
the two `rom_loader` tests and the asset viewer's check on your ROM; CI has neither, so there they are not built. To
build exactly what CI builds, point `TD_DECOMP_DIR` at a folder that does not exist, in a build folder of its own. From
the repository folder:

```
cmake -S . -B build/no-game-code -G Ninja -DCMAKE_BUILD_TYPE=Debug -DTD_DECOMP_DIR=/nonexistent
cmake --build build/no-game-code
ctest --test-dir build/no-game-code
```

**Windows**: install Visual Studio 2022 with the "Desktop development with C++" workload, and Git for Windows. A
Windows build needs its own clone on the Windows side: a copy inside WSL cannot be built from Windows. Open the
"x64 Native Tools Command Prompt for VS 2022" (Start menu, Visual Studio 2022 folder; it sets up the 64-bit compiler,
which the "Developer PowerShell" does not) and run:

```
git clone --recursive https://github.com/soulsdweeb212/turnbuckle-dive.git C:\src\turnbuckle-dive
cd C:\src\turnbuckle-dive
cmake --preset windows-relwithdebinfo
cmake --build --preset windows-relwithdebinfo
ctest --preset windows-relwithdebinfo
cd build\windows-relwithdebinfo && hello.exe --check --frames 30
```

The first configure downloads vcpkg into `build\_vcpkg` and builds the libraries, which takes a few minutes. Deleting
the `build` folder deletes them too.

## Adding to CI, or setting it up from nothing

**Adding a test.** `ci.yml` does not change. Tests are listed in `CMakeLists.txt`, inside `if(BUILD_TESTING)` blocks,
with `add_test`, and `ctest` runs every test listed there, on your computer and on both runners (except the tests that
need the game's code, below). For example, the `rom_missing` test is:

```cmake
add_test(NAME rom_missing COMMAND turnbuckle-dive ${CMAKE_BINARY_DIR}/no-rom-here.z64)
set_tests_properties(rom_missing PROPERTIES PASS_REGULAR_EXPRESSION "no-rom-here\\.z64: file not found")
```

A test passes when its program ends with exit code 0, or, as here, when its output contains the given text. Either
way it fails when its output holds a report of AddressSanitizer, LeakSanitizer or UndefinedBehaviorSanitizer (an
`asan` build): the last lines of `CMakeLists.txt` set that for every test in the file, so a new test goes above them.

The tests inside an `if(TD_GAME_CODE)` part, such as `rom_loader` and `viewer_check`, exist only in builds with the
game's code, so CI never builds or runs them. A test that must run on CI goes outside those parts, and must need
neither the ROM nor the decompilation.

**Adding a step.** Add an entry under a job's `steps:`, indented exactly like the others:

```yaml
      - name: Show the compiler's version
        run: gcc --version
```

`name:` is what GitHub shows, and `run:` is the command. Steps run in the order they appear. On Linux, several
commands can share a step: write `run: |` and put each command on its own line below it, indented further (the install
step in `ci.yml` does this); bash stops at the first one that fails. On Windows, give each command its own step (see
"Pass or fail").

**Adding another system** (macOS, for example). Copy a whole job, from its key (such as `linux:`) to the end of its
steps, give it a new key and `name:`, and set `runs-on:` to that system's runner (for example `macos-15`). Then change
the steps that differ:

- how the tools are installed (Homebrew's `brew install` on macOS instead of `apt-get`);
- every step that names a preset: add a configure, a build and a test preset with the same name to
  `CMakePresets.json`, limited to that system by a `condition` like the existing ones;
- "Check hello's picture": `xvfb-run` and the Mesa packages exist only on the Linux runner, so start hello directly
  and put `timeout-minutes:` on the step instead of the `timeout` command. Whether hello can draw and pass `--check` on
  a new system has to be tried there first.

**Trying changes on a branch.** CI runs on every branch, so changes to `ci.yml` can be tried on a branch of their own
and merged into `main` once that branch's run is green:

```
git checkout -b ci-change
git add .github/workflows/ci.yml
git commit -m "Say what the change does"
git push -u origin ci-change
```

**Setting it up from nothing**, in a new repository:

1. In the repository folder, create the folder `.github/workflows` and, inside it, a file whose name ends in `.yml`,
   for example `ci.yml`. A copy of this project's file is a good start.
2. The smallest recipe that works looks like this:

   ```yaml
   name: CI
   on:
     push:
   jobs:
     build:
       runs-on: ubuntu-24.04
       steps:
         - uses: actions/checkout@v7
         - run: echo "The code is here and the runner works."
   ```

3. Commit the file and push it. The run appears on the repository's Actions tab. (Actions must be allowed in the
   repository's Settings, under Actions, then General; they are by default.)
4. Add steps one at a time, pushing after each change, until the recipe builds and tests everything.
5. Add the badge to the README, with `OWNER/REPOSITORY` replaced (for this project, `soulsdweeb212/turnbuckle-dive`):

   ```
   [![CI](https://github.com/OWNER/REPOSITORY/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/OWNER/REPOSITORY/actions/workflows/ci.yml)
   ```

## Keeping CI working

- The list of `apt` packages is in two places, README.md and ci.yml. Change both together.
- The machines are named in ci.yml (`ubuntu-24.04`, `windows-2022`). GitHub retires old ones after a few years; then
  the name is changed to a newer one (for example `windows-2025`).
- The actions CI uses have versions in ci.yml (`actions/checkout@v7`, `actions/cache/...@v6`,
  `ilammy/msvc-dev-cmd@v1`), raised when GitHub marks one as outdated.
- LibUltraShip's CMake updates vcpkg to its newest version on every Windows configure, so the library versions on
  Windows can change with no change in this repository (on 2026-11-08 Windows got SDL3 3.4.20, while Linux builds the
  3.4.12 pinned in `cmake/libultraship.cmake`).
- The saved vcpkg zips are listed under Actions, then Caches. Deleting them is safe: the next run builds the libraries
  again.
