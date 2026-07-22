# Packaged QEMU runtime

This directory contains the exact DLL set copied into the MSIX package:

- `Qemu` contains the `qemu-system-*` host DLLs.
- `Runtime` contains their SDL2, zlib, and MSYS2/UCRT runtime dependencies.

The project file intentionally references only these relative paths. When updating QEMU, replace the DLLs as one tested set and verify that every target exports `qemu_host_get_api_version`. That export returns an unsigned integer with the ABI major version in the high 16 bits and the minor version in the low 16 bits. This host currently accepts major version `1`; legacy DLLs without the export remain supported through required-export validation and are reported as such in the diagnostics log.
