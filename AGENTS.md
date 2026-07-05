# Deskflow Agent Instructions

## Communication and Git

- Reply to the user in Korean.
- Write repository artifacts, documentation, code comments, and commit messages in English.
- Do not create a git commit unless the user explicitly requests it.
- Use Conventional Commits with bullet-point bodies when committing.
- Do not run browser/manual app tests unless the user explicitly asks; the user handles those checks.

## Windows Build Server

- The Windows 11 build server is `192.168.0.5`.
- The Windows workspace path is `Z:\@Development\deskflow`.
- Any Deskflow code changes made in this repository must be synced to the Windows workspace and built there before the work is considered complete.
- Before running a freshly built Windows binary, stop any existing Deskflow processes on the Windows host.
- Use the repository `dev` wrapper on Windows for normal development builds.
- After the Windows build succeeds, generate the Windows MSI on the Windows host.
- Generate MSI-only output with `cpack -G WIX --config .\CPackConfig.cmake` from `Z:\@Development\deskflow\build\windows-debug-vcpkg`; do not use the CMake `package` target for routine Windows packaging because it also runs the configured `7Z` generator.
- For MSI builds, enable `BUILD_INSTALLER=ON`; the `dev` build preset only exposes development binary targets and does not expose the CPack `package` target.
- MSI packaging requires WiX Toolset v4 with `WixToolset.Firewall.wixext`, `WixToolset.Util.wixext`, and `WixToolset.UI.wixext`.
