# Post-Code-Change Workflow

This workflow is mandatory after every Deskflow code change.

## Completion Gate

A code change is complete only after all of the following are done:

- Commit the completed change with a Conventional Commit message.
- Push the commit to the configured remote branch.
- Build and replace binaries on the local macOS computer.
- Build and replace binaries on the Windows 11 server PC.
- Build and replace binaries on the Windows 11 client PC.

If a required machine is unreachable or not configured, record the blocker and
do not describe the code change as fully complete.

## Target Inventory

| Target | Role | Host | Workspace | Required action |
| --- | --- | --- | --- | --- |
| Local macOS | Local validation | `ruru.mac` (`192.168.0.6`) | `/Volumes/AI.DEV/@Dev/deskflow` | Build `macos-release`, install `/Applications/Deskflow.app`, restart Deskflow |
| Windows 11 server PC | Server validation | `ishtar.win` (`RURUBROS`, `192.168.0.5`) | `Z:\@Development\deskflow` | Sync, build, generate Release MSI, replace binaries, launch GUI |
| Windows 11 client PC | Client validation | `ruru.win` (`ZEN-WINDOWS7`, `192.168.0.4`, SSH user `rurugrabssh`, port `23`) | `D:\@Development\deskflow` | Inbound sync, build, install the Release MSI from `ishtar.win`, restart Deskflow |

The Windows client procedure is maintained in `docs/dev/windows_client.md`.

## macOS Build And Replacement

Build the local macOS app:

```sh
cmake --preset macos-release
cmake --build --preset macos-release-gui
cmake --build --preset macos-release-core
```

Stop the installed app before replacement:

```sh
osascript -e 'tell application "Deskflow" to quit' || true
pkill -f '/Applications/Deskflow.app/Contents/MacOS' || true
```

Install and restart:

```sh
cmake --build --preset macos-install-app-fast
open -a /Applications/Deskflow.app
```

Use `macos-install-app` when the installed app bundle is missing frameworks or
plugins.

## Windows Server Build And Replacement

The Windows 11 server PC is documented in
`docs/dev/windows_build_server.md`.

Minimum required steps:

```sh
ssh ishtar.win 'powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location -LiteralPath ''Z:\@Development\deskflow''; .\dev.ps1 dev"'
```

Then generate the MSI from the Windows Release preset:

```powershell
$env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
cmd.exe /d /s /c "call `"$vsDevCmd`" -arch=x64 -host_arch=x64 >nul && cd /d `"Z:\@Development\deskflow`" && cmake --preset windows-release-vcpkg && cmake --build --preset windows-release-vcpkg-msi"

Set-Location -LiteralPath 'Z:\@Development\deskflow\build\windows-release-vcpkg'
cpack -G WIX --config .\CPackConfig.cmake
```

Install or otherwise replace the server PC binaries with the generated Release
MSI. Do not use Debug MSI outputs. A silent MSI install starts the service/core
path but does not guarantee that the GUI is visible on the logged-in desktop;
launch `deskflow.exe` in the active user session as documented in
`docs/dev/windows_build_server.md`.

## Windows Client Build And Replacement

The Windows 11 client PC must follow the same principles as the server PC:

- Sync the same commit and any required working-tree changes.
- Build using the repository `dev` wrapper for development validation.
- Generate or use Release binaries for installed binary replacement.
- Stop existing Deskflow processes before replacing binaries.
- Restart Deskflow after replacement.

The host is `ruru.win` (`ZEN-WINDOWS7`, `192.168.0.4`) and its SSH service
listens on port `23` for account `rurugrabssh`. Key-based access from
`ruru.mac` and `ishtar.win` is verified. Its workspace is
`D:\@Development\deskflow`. Follow `docs/dev/windows_client.md` for inbound
sync, local build, MSI replacement, and process verification. Do not configure
outbound access from `ruru.win` to the other local machines.

## Git Requirements

Use Conventional Commits with a bullet-point body. Example:

```text
fix(mac): restore clipboard refresh after local copy

- Add a fast pasteboard check after Command-C and Command-X
- Keep polling as the correctness fallback for non-keyboard clipboard updates
- Document the macOS validation workflow
```

After commit:

```sh
git push
```
