# QEMU UWP Host

QEMU UWP Host is a Universal Windows Platform host application for running embedded QEMU system emulators through packaged `qemu-system-*` DLLs. It provides a graphical frontend for selecting a QEMU target, choosing boot media, generating QEMU command lines, and displaying video output through the host display backend.

## Features

- Dynamic QEMU target selection from packaged `qemu-system-*` DLLs.
- Host display rendering inside the UWP application.
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

The app detects packaged QEMU targets automatically by scanning for DLLs named `qemu-system-*.dll`. The target selector is populated from those DLLs at startup, with `x86_64` used as the default target when available.

Supported target entries depend on the DLLs included in the package. Typical targets include:

- `x86_64`
- `i386`
- `aarch64`
- `arm`
- `riscv64`
- `riscv32`
- `ppc64`
- `ppc`
- `mips64`
- `mips`
- `s390x`
- `sparc64`
- `sparc`
- `m68k`
- `loongarch64`
- `shadps4` — not working.

## Qemu shared library

Modified QEMU 11.0.2 files to build as a shared library in https://github.com/rodrigoandrigo/Qemu-Dll-shadps4

## Boot Workflow

1. Select the QEMU target.
2. Choose a profile or keep the normal command profile.
3. Select drive media, CD-ROM media, or direct boot files.
4. Review the generated command in the `qemu commands` tab.
5. Add any custom arguments in `Additional QEMU arguments`.
6. Start the emulator from the command bar.

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

Profiles can be used as a starting point and then adjusted through the selectors or through additional QEMU arguments.

## Media

The media section supports selecting drive and CD-ROM images and tracks previously used media from the app-managed boot media folder. Supported image formats include common QEMU disk and optical media formats such as RAW, ISO, QCOW2, VDI, VHD/VHDX, VMDK, DMG, QED, VPC/VHD, VVFAT, and Parallels images.

## Commands

The `qemu commands` tab shows the command that will be written and executed for the selected target and profile. It distinguishes generated arguments from custom additional arguments, can reset the command view to startup defaults, and can format the command vertically for easier inspection.

## Input

The host forwards keyboard input and uses captured mouse input for the emulator surface. Mouse handling is designed around absolute pointer coordinates, using tablet-style guest input for targets and profiles where that device is available.

Use `Ctrl + Alt + M` to release or recapture emulator input.

## Diagnostics

The Errors tab collects runtime messages, generated command information, QEMU host API inspection results, and packaged DLL load diagnostics. These tools help verify that the packaged QEMU DLLs and dependencies are available to the app at runtime.

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

Before starting a VM, the app runs a preflight check for the selected target DLL, direct boot files, and RFB settings. Results are recorded in the Errors tab.

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
