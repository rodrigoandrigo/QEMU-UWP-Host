# QEMU UWP Host

QEMU UWP Host is a Universal Windows Platform host application focused on the packaged `qemu-system-x86_64` emulator. It provides a graphical frontend for choosing boot media, generating QEMU commands, and displaying video output through the host display backend.

## Features

- Fixed `qemu-system-x86_64` target for the Xbox-focused build.
- Host display rendering inside the UWP application.
- Dirty-rectangle framebuffer transfer and partial Direct3D texture uploads with full-frame fallback for older DLLs.
- On-screen runtime metrics for first-frame boot time, process RAM, received-frame FPS, and normalized process CPU usage.
- XAudio2 audio configuration for generated profiles.
- Boot media selection for drive and CD-ROM images.
- Boot media history from the app-managed media folder.
- Automatic command generation with a dedicated `qemu commands` view.
- Target-aware selectors for machines, CPUs, devices, VGA, monitor, networking, USB, input, and audio devices.
- Built-in profiles for common startup scenarios and guest generations.
- Direct kernel boot fields for firmware, kernel, initrd, DTB, and kernel append arguments.
- Absolute mouse input using tablet-style guest input when available.
- Keyboard input forwarding, including special keys.
- Error and diagnostic views for runtime logs and packaged DLL load checks.

## QEMU Targets

The only available target is `x86_64`, backed by `qemu-system-x86_64.dll`. Other packaged QEMU DLLs are not exposed as selectable targets.

## Boot Workflow

1. Choose a profile or keep the normal command profile.
2. Select drive media, CD-ROM media, or direct boot files.
3. Review the generated command in the `qemu commands` tab.
4. Add any custom arguments in `Additional QEMU arguments`.
5. Start the emulator from the command bar.

The generated command is target-aware. Machine defaults, boot device handling, disk interface choices, firmware selection, and input defaults are adapted to the selected target.

## Profiles

Profiles provide practical startup presets for common use cases:

- Normal command
- Video only no media
- Linux cloud qcow2
- Linux cloud UEFI qcow2
- Ubuntu 10
- Ubuntu 12
- Windows 98
- Windows ME
- Windows XP
- Windows Vista
- Windows 7
- Xbox TCG 64 MB cache (2 vCPUs)
- Xbox TCG 128 MB cache (2 vCPUs)
- Xbox TCG 256 MB cache (4 vCPUs)

Profiles can be used as a starting point and then adjusted through the selectors or through additional QEMU arguments.
Every generated `x86_64` command uses `-accel tcg,thread=multi`. The balanced default translation cache is 128 MB, avoiding QEMU's much larger generic 64-bit default reservation on the memory-constrained Xbox/UWP host. The default is 2 vCPUs, with 4 and 6 vCPUs available in the selector. Xbox TCG profiles set the translation cache to 64, 128 or 256 MB and select their matching 2- or 4-vCPU configuration; 6 vCPUs can be selected manually. Guest memory, machine, CPU, video and device selections remain independently configurable.

## Media

The media section supports selecting drive and CD-ROM images and tracks previously used media from the app-managed boot media folder. Supported image formats include common QEMU disk and optical media formats such as RAW, ISO, QCOW2, VDI, VHD/VHDX, VMDK, DMG, QED, VPC/VHD, VVFAT, and Parallels images.

## Commands

The `qemu commands` tab shows the command that will be written and executed for the selected target and profile. It distinguishes generated arguments from custom additional arguments, can reset the command view to startup defaults, and can format the command vertically for easier inspection.

## Input

The host forwards keyboard input and uses captured mouse input for the emulator surface. Mouse handling is designed around absolute pointer coordinates, using tablet-style guest input for targets and profiles where that device is available.

Use `Ctrl + Alt + M` to release or recapture emulator input.

## Diagnostics

The Errors tab collects runtime messages, generated command information, QEMU host API inspection results, and packaged DLL load diagnostics. These tools help verify that the packaged QEMU DLLs and dependencies are available to the app at runtime.

## UWP JIT and `codeGeneration`

The Xbox/UWP QEMU build uses `codeGeneration` with split W^X memory. The TCG translation cache is backed by one paging-file mapping exposed through two coherent views: RW for emitting translated code and RX for executing it. No mapped page is writable and executable at the same time. Publishing a translation also calls `FlushInstructionCache`, including on x86-64, as required by the packaged-app API contract.

The Windows SDK UWP source path uses `CreateFileMappingFromApp`, `MapViewOfFileFromApp`, `VirtualProtectFromApp`, and `VirtualAllocFromApp`; Meson detects and links `OneCore.lib`. The legacy Durango XDK lacks the latter two `FromApp` declarations, so that toolchain automatically retains its permitted TV_APP `VirtualAlloc` and `VirtualProtect` calls while still using split RW/RX mappings. Both paths reject an attempted RWX protection. The package manifest must retain the `codeGeneration` capability.

After compiling your QEMU DLL, validate its source contract and imports from a Developer PowerShell:

```powershell
& <qemu-source>\scripts\ci\check-uwp-jit.ps1 `
    -BinaryPath <path-to-qemu-system-x86_64.dll>
```

Omit `-BinaryPath` to perform only the static source verification. The script does not compile QEMU.

For a DLL compiled against the July 2018 Durango XDK headers, add `-DurangoXdk` so the import audit accepts the XDK's TV_APP memory APIs.

## Project Structure

- `App.xaml` and related files define the UWP application shell.
- `DirectXPage.xaml` and related files implement the main UI, command generation, profiles, input capture, and diagnostics.
- `QemuDirectHost` loads the selected QEMU system DLL, resolves the host embedding API, and forwards video, audio, keyboard, mouse, and control events.
- `Content` contains the DirectX frame renderer.
- `Common` contains shared DirectX device and helper code.
- `qemu` contains packaged QEMU firmware and BIOS resources.

## Build

The project is a UWP C++ application built with MSBuild and the Windows SDK. QEMU system DLLs and required runtime DLLs are packaged as content files, together with firmware resources used by generated commands.

The checked-in runtime is self-contained:

- `Dependencies\Qemu` contains the QEMU system DLLs.
- `Dependencies\Runtime` contains their runtime dependencies.
- `qemu` contains firmware and BIOS resources.

No MSYS2 installation or sibling QEMU DLL directory is needed to package the application. Update these DLLs as one tested set; see `Dependencies\README.md` and `QEMU_HOST_API.md` for the host-API compatibility contract.

Before starting a VM, the app runs a preflight check for the selected target DLL and direct boot files. Results are recorded in the Errors tab. Xbox/UWP always uses the local in-process Direct3D display; VNC/RFB, DBus display, WHPX and automatically detected OpenGL/cURL support are excluded from this TCG-only build path.

Host API 1.3 also lets the embedded QEMU main loop block on its native event wait and wake only for timers, devices, input or lifecycle requests. Older DLLs remain compatible through the previous nonblocking fallback.

### Requirements

- Visual Studio with C++ UWP tooling.
- Windows 10/11 SDK with MSBuild, MakeAppx, and SignTool.
- The project test certificate `Qemu-UWP-host_TemporaryKey.pfx`.
- Packaged QEMU target DLLs and runtime dependencies already staged in the project dependency folders.

### Build Steps

1. Open a Developer PowerShell or Developer Command Prompt for Visual Studio.
2. Build the x64 Debug package with MSBuild:

   ```powershell
   msbuild Qemu-UWP-host.vcxproj /p:Configuration=Debug /p:Platform=x64 /m
   ```

3. MSBuild compiles the C++/CX UWP app, regenerates XAML files, copies packaged assets and QEMU runtime files into the package layout, creates the `.msix` files, and bundles them into a `.msixbundle`.
4. The generated bundle is written under:

   ```text
   AppPackages\Qemu-UWP-host\<version>_Debug_Test\
   ```

### Signing

Sign the generated `.msixbundle` with the project certificate:

```powershell
signtool sign /fd SHA256 /f Qemu-UWP-host_TemporaryKey.pfx AppPackages\Qemu-UWP-host\<version>_Debug_Test\Qemu-UWP-host_<version>_x64_Debug.msixbundle
```

Validate the signature:

```powershell
signtool verify /pa /v AppPackages\Qemu-UWP-host\<version>_Debug_Test\Qemu-UWP-host_<version>_x64_Debug.msixbundle
```

A valid package reports zero verification errors.

### Build Notes

- The package version comes from `Package.appxmanifest`.
- The app must be signed before installation or deployment outside the Visual Studio debugger.
- If QEMU DLLs are updated, rebuild the app package so the new DLL set is copied into the package layout.
- The app expects the packaged `qemu` firmware folder to be present inside the package.
- PRI warnings about asset qualifiers do not necessarily block package creation, but signing and verification must still pass.
