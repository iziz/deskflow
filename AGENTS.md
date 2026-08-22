# Deskflow Agent Instructions

## Communication and Git

- Reply to the user in Korean.
- Write repository artifacts, documentation, code comments, and commit messages in English.
- Do not create a git commit unless the user explicitly requests it.
- For Deskflow code changes, the user has given a standing explicit instruction
  to commit and push after completing the required local and Windows builds.
- Use Conventional Commits with bullet-point bodies when committing.
- Do not run browser/manual app tests unless the user explicitly asks; the user handles those checks.

## Post-Code-Change Completion Gate

- After any Deskflow code change, do not report the work complete until the
  change has been committed and pushed.
- After any Deskflow code change, build and replace the binaries on all active
  validation machines:
  - This macOS computer (`ruru.mac`, `192.168.0.6`): build `macos-release`, install to
    `/Applications/Deskflow.app`, and restart the app.
  - Windows 11 server PC (`ishtar.win`): sync/build in `Z:\@Development\deskflow`, generate the
    Release MSI, and replace the installed/running binaries.
  - Windows 11 client PC (`ruru.win`, `ZEN-WINDOWS7`, `192.168.0.4`, SSH port
    `23`): sync/build in `D:\@Development\deskflow`, install the Release MSI
    generated on `ishtar.win`, and restart the service/core processes.
- If any required validation machine is not reachable or not yet configured,
  report the block explicitly and do not treat the code change as fully
  complete.

## Windows Build Server

- The Windows 11 build server is `192.168.0.5`.
- Use the SSH alias `ishtar.win` for routine access to the Windows build server.
- The Windows workspace path is `Z:\@Development\deskflow`.
- Any Deskflow code changes made in this repository must be synced to the Windows workspace and built there before the work is considered complete.
- Before running a freshly built Windows binary, stop any existing Deskflow processes on the Windows host.
- Use the repository `dev` wrapper on Windows for normal development builds.
- After the Windows development build succeeds, generate the Windows MSI from the `windows-release-vcpkg` preset on the Windows host.
- Generate MSI-only output with `cpack -G WIX --config .\CPackConfig.cmake` from `Z:\@Development\deskflow\build\windows-release-vcpkg`; do not use the CMake `package` target for routine Windows packaging because it also runs the configured `7Z` generator.
- Do not ship or install an MSI generated from `windows-debug-vcpkg`; Debug outputs depend on debug MSVC runtime DLLs such as `MSVCP140D.dll`, `VCRUNTIME140D.dll`, and `ucrtbased.dll`.
- MSI packaging requires WiX Toolset v4 with `WixToolset.Firewall.wixext`, `WixToolset.Util.wixext`, and `WixToolset.UI.wixext`.

## macOS Local Validation

- After changing macOS input, clipboard, app, or core behavior, build the `macos-release` app and install it to `/Applications/Deskflow.app` before treating the local macOS behavior as updated.
- Stop the running `/Applications/Deskflow.app` processes before installing; `install-macos-app-fast` refuses to update a running app.
- Use `cmake --preset macos-release`, then `cmake --build --preset macos-release-gui`, `cmake --build --preset macos-release-core`, and `cmake --build --preset macos-install-app-fast` for the normal local update path when the destination app already has deployed frameworks.
- Restart `/Applications/Deskflow.app` after installing so the GUI and `deskflow-core` use the rebuilt binaries.

## Windows Client

- The Windows 11 client is `ruru.win` (`ZEN-WINDOWS7`) at `192.168.0.4`; its
  SSH service listens on port `23`.
- Use SSH account `rurugrabssh`; key-based access from `ruru.mac` and
  `ishtar.win` is verified.
- The workspace path is `D:\@Development\deskflow`.
- Synchronize source, dependencies, build tools, and installers inbound from
  `ruru.mac` or `ishtar.win`; do not configure outbound access from `ruru.win`.
- Build with the repository `dev` wrapper. When Visual Studio's bundled
  CMake/Ninja component is absent, use the inbound tool copy at
  `D:\@Development\tools\CMake` on `PATH`.
- Replace the installed binaries with the Release MSI generated on
  `ishtar.win`, then restart and verify the Deskflow service/core processes.
- See `docs/dev/windows_client.md` for the maintained procedure.
