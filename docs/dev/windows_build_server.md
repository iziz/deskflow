# Windows Build Server Workflow

This project uses a Windows 11 build server for Windows validation and installer
creation.

## Host

- Host: `192.168.0.5`
- Workspace: `Z:\@Development\deskflow`
- Usual Windows account: `zenis`

## Invariants

- Sync every local Deskflow code change to the Windows workspace before
  reporting the work as complete.
- Kill existing Deskflow processes on the Windows host before running the newly
  built binaries.
- Build from the Windows workspace, not from the macOS checkout.
- Use the repository `dev` wrapper for normal development builds.
- Generate the MSI installer after the Windows build succeeds.
- Do not build the portable `7Z` package for routine Windows validation.

## SSH Access

The preferred remote command path is OpenSSH:

```sh
ssh zenis@192.168.0.5
```

If the Windows username changes, update this document and `AGENTS.md`.

## Process Cleanup

Run this on the Windows host before launching built binaries or before a clean
validation run:

```powershell
Get-Process Deskflow,deskflow-core,deskflow-daemon -ErrorAction SilentlyContinue | Stop-Process -Force
```

From macOS through SSH:

```sh
ssh zenis@192.168.0.5 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Process Deskflow,deskflow-core,deskflow-daemon -ErrorAction SilentlyContinue | Stop-Process -Force"'
```

## Sync Verification

After syncing local changes to the Windows workspace, verify the Windows working
tree before building:

```powershell
Set-Location -LiteralPath 'Z:\@Development\deskflow'
git status --short
```

The Windows status must include the same intended source changes that are being
validated from macOS. Do not rely on a macOS-only build for Windows validation.

## Build

Run the normal development build from the Windows workspace:

```powershell
Set-Location -LiteralPath 'Z:\@Development\deskflow'
.\dev.ps1 dev
```

The equivalent `cmd.exe` wrapper is:

```cmd
cd /d Z:\@Development\deskflow
dev.cmd dev
```

From macOS through SSH:

```sh
ssh zenis@192.168.0.5 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location -LiteralPath ''Z:\@Development\deskflow''; .\dev.ps1 dev"'
```

## Installer/Package

After the development build succeeds, build the installer from the Windows
Release preset. Do not create installers from the Debug build directory: Debug
binaries and custom actions depend on debug MSVC runtime DLLs such as
`MSVCP140D.dll`, `VCRUNTIME140D.dll`, and `ucrtbased.dll`, which are not valid
installer dependencies.

Configure and build the Release binaries, application translations, and WiX
custom action inside the Visual Studio development environment:

```powershell
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cd /d `"Z:\@Development\deskflow`" && cmake --preset windows-release-vcpkg && cmake --build --preset windows-release-vcpkg-msi"
```

Generate only the MSI with CPack's WIX generator:

```powershell
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
Set-Location -LiteralPath 'Z:\@Development\deskflow\build\windows-release-vcpkg'
cpack -G WIX --config .\CPackConfig.cmake
```

WiX Toolset v4 is required for MSI output. If it is missing, install it for the
Windows user and add the required extensions:

```powershell
dotnet tool install --global wix --version 4.0.6
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
wix extension add WixToolset.Firewall.wixext/4.0.6
wix extension add WixToolset.Util.wixext/4.0.6
wix extension add WixToolset.UI.wixext/4.0.6
```

Do not install WiX v7 for unattended packaging unless the operator has reviewed
and accepted its OSMF EULA requirement. The expected MSI output is:

```text
Z:\@Development\deskflow\build\windows-release-vcpkg\deskflow-<version>-win-x64.msi
```

Avoid `cmake --build --preset windows-debug-vcpkg --target package` for routine
MSI generation. That target runs every configured CPack generator, including the
portable `7Z` package, and can also build unrelated test targets before
packaging.

## Confirmed Windows Flow

The current verified flow from macOS is:

```sh
ssh zenis@192.168.0.5 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location -LiteralPath ''Z:\@Development\deskflow''; .\dev.ps1 dev"'
```

Then, from a PowerShell SSH session:

```powershell
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cd /d `"Z:\@Development\deskflow`" && cmake --preset windows-release-vcpkg && cmake --build --preset windows-release-vcpkg-msi"

Set-Location -LiteralPath 'Z:\@Development\deskflow\build\windows-release-vcpkg'
cpack -G WIX --config .\CPackConfig.cmake
```

## Confirmed Issues

- `cmake --build --preset dev --target package` fails because the `dev` build
  preset only exposes development binary targets.
- `cmake --build --preset windows-debug-vcpkg --target package` creates both
  `7Z` and `WIX` outputs because `deploy/windows/deploy.cmake` configures both
  generators. Use `cpack -G WIX` for MSI-only packaging.
- An MSI generated from `windows-debug-vcpkg` can fail during installation with
  missing debug runtime DLLs. Confirmed dependencies include `MSVCP140D.dll`,
  `VCRUNTIME140D.dll`, `VCRUNTIME140_1D.dll`, and `ucrtbased.dll`.
- The MSI input build must include `app_translations`; otherwise CPack install
  can fail because `translations/deskflow_*.qm` files have not been generated.
- Running CMake build commands outside the Visual Studio development environment
  can fail with compiler detection errors or missing standard headers such as
  `type_traits` and `cstdint`.
- Existing `deskflow.exe` or `deskflow-core.exe` processes can lock build
  outputs and cause `LNK1168`. Use `taskkill /F /IM deskflow.exe /IM
  deskflow-core.exe /IM deskflow-daemon.exe /T` if `Stop-Process` does not
  release them.
- WiX v7 currently requires OSMF EULA acceptance before extension installation.
  Use WiX v4.0.6 for unattended MSI generation.
- If macOS archive-based sync is used for untracked files, disable AppleDouble
  metadata or remove `._*` files from the Windows workspace before building.

## Notes

- If `Z:` is not available inside an SSH session, verify that it is a real
  volume for the SSH user and not only an interactive desktop mapped drive.
- If OpenSSH starts in a different shell, explicitly invoke PowerShell with
  `powershell -NoProfile -ExecutionPolicy Bypass -Command`.
- Do not treat a successful macOS build as a substitute for the Windows build
  and installer/package workflow.
