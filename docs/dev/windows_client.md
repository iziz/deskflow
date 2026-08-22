# Windows Client Workflow

This workflow validates and replaces Deskflow on the Windows 11 client PC.

## Host

- Canonical name: `ruru.win`
- Computer name: `ZEN-WINDOWS7`
- Address: `192.168.0.4`
- SSH account: `rurugrabssh`
- SSH port: `23`
- Workspace: `D:\@Development\deskflow`
- Installed application: `C:\Program Files\Deskflow`

## Access Boundary

- Initiate source, dependency, tool, and installer transfers from `ruru.mac` or
  `ishtar.win`.
- Do not configure `ruru.win` for outbound access to either local machine.
- Keep private SSH keys on their originating machines.

## Workspace Sync

Mirror the committed `ishtar.win` workspace into
`D:\@Development\deskflow`. Include the repository metadata and `deps`
directory, but exclude existing `build` directories so `ruru.win` produces its
own build outputs.

The transfer must be initiated from `ruru.mac` or `ishtar.win`. After the
transfer, verify the copied revision and working tree from the client workspace
before building.

## Build Tools

The host has Visual Studio 2022 Community with the x64 MSVC toolchain and the
Windows 11 SDK 10.0.26100 component. Its Visual Studio installation does not
include the bundled CMake/Ninja or Git components, so keep the verified inbound
tool copies at:

```text
D:\@Development\tools\CMake
D:\@Development\tools\Git
```

Add its executable directories to `PATH` for each remote build session:

```powershell
$env:PATH = 'D:\@Development\tools\Git\cmd;D:\@Development\tools\CMake\CMake\bin;D:\@Development\tools\CMake\Ninja;' + $env:PATH
```

## Development Build

Use the repository wrapper so the Visual Studio x64 environment is applied
consistently:

```powershell
$env:PATH = 'D:\@Development\tools\Git\cmd;D:\@Development\tools\CMake\CMake\bin;D:\@Development\tools\CMake\Ninja;' + $env:PATH
Set-Location -LiteralPath 'D:\@Development\deskflow'
.\dev.ps1 configure
.\dev.ps1 dev
```

Do not use Debug outputs for installed binary replacement.

## Release Installation

Generate the Release MSI on `ishtar.win` according to
`docs/dev/windows_build_server.md`, then transfer it inbound to:

```text
D:\@Development
```

Stop the installed service and processes before running the installer:

```powershell
$msi = 'D:\@Development\deskflow-<version>-win-x64.msi'
Stop-Service -Name Deskflow -Force -ErrorAction SilentlyContinue
Get-Process deskflow,deskflow-core,deskflow-daemon -ErrorAction SilentlyContinue |
  Stop-Process -Force
Start-Process -FilePath msiexec.exe -ArgumentList @('/i', $msi, '/qn', '/norestart') -Wait
Start-Service -Name Deskflow
Remove-Item -LiteralPath $msi -Force
```

## Verification

Verify the GUI application and the service/core runtime as separate surfaces.
The installed registry version and all three binary versions and hashes must
match the Release MSI:

```powershell
Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
  'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*' |
  Where-Object DisplayName -Like '*Deskflow*' |
  Select-Object DisplayName,DisplayVersion,InstallLocation

(Get-Item 'C:\Program Files\Deskflow\deskflow.exe').VersionInfo.FileVersion
(Get-Item 'C:\Program Files\Deskflow\deskflow-core.exe').VersionInfo.FileVersion
(Get-Item 'C:\Program Files\Deskflow\deskflow-daemon.exe').VersionInfo.FileVersion
Get-FileHash -Algorithm SHA256 'C:\Program Files\Deskflow\deskflow.exe',
  'C:\Program Files\Deskflow\deskflow-core.exe',
  'C:\Program Files\Deskflow\deskflow-daemon.exe'
```

Verify the GUI Startup shortcut independently. Resolve the shortcut by its
target name instead of relying on its localized filename:

```powershell
$account = (Get-CimInstance Win32_ComputerSystem).UserName.Split('\')[-1]
$startup = Join-Path (Join-Path 'C:\Users' $account) `
  'AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup'
$shortcutFile = Get-ChildItem -LiteralPath $startup -Filter '*.lnk' -File |
  Where-Object Name -Like '*deskflow*' |
  Select-Object -First 1
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutFile.FullName)
[pscustomobject]@{
  Path = $shortcutFile.FullName
  Target = $shortcut.TargetPath
  WorkingDirectory = $shortcut.WorkingDirectory
  TargetExists = (Test-Path -LiteralPath $shortcut.TargetPath)
}
```

The target must be `C:\Program Files\Deskflow\deskflow.exe`. The former
`D:\@Programs\deskflow\deskflow.exe` path no longer exists. If the GUI is
expected to be active in the current interactive session, confirm that
`deskflow.exe` is running in that session; an installed binary alone is not
runtime evidence.

Verify the service, daemon, client core, and active server connection:

```powershell
Get-Service -Name Deskflow
Get-Process deskflow,deskflow-core,deskflow-daemon -ErrorAction SilentlyContinue |
  Select-Object ProcessName,Id,SessionId,Path,StartTime
Get-NetTCPConnection -RemoteAddress 192.168.0.5 -RemotePort 24801 |
  Select-Object State,LocalAddress,LocalPort,RemoteAddress,RemotePort,OwningProcess
```

Do not treat the client update as complete unless the local build succeeds and
the GUI installation and Startup target are verified independently from the
service/core processes running from `C:\Program Files\Deskflow`.
