# QEMU host API compatibility

`QemuDirectHost` loads `qemu-system-*.dll` through the following ABI contract.

## Version negotiation

New host DLLs must export this C ABI function:

```c
uint32_t qemu_host_get_api_version(void);
```

The return value is `major << 16 | minor`. Qemu-UWP-host accepts API major `1` and may use newer minor versions when all required exports below remain compatible. A DLL with another major version is rejected before the emulator is initialized.

Older unversioned DLLs continue to work temporarily: the app validates every required export and records that the compatibility result is inferred. New releases should implement the version export so incompatibilities are deterministic.

## Required exports for API 1.x

- `qemu_host_init`, `qemu_host_start`, `qemu_host_main_loop_step`
- `qemu_host_request_shutdown`, `qemu_host_reset`, `qemu_host_join`, `qemu_host_cleanup`
- `qemu_host_send_key_number`, `qemu_host_send_key_qcode`
- `qemu_host_send_pointer_abs`, `qemu_host_send_pointer_button`
- `qemu_host_is_initialized`, `qemu_host_is_running`
- one video callback registration: `qemu_host_register_video_callback` or `qemu_host_set_video_callback`

Callback registration, input and lifecycle entry points must keep the calling conventions and parameter layout declared in `QemuDirectHost.h`.
