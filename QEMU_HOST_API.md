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

## Dirty rectangles in API 1.2

API 1.2 adds the optional `qemu_host_register_video_update_callback` export. Its callback receives the complete surface pointer and dimensions plus an `(x, y, width, height)` dirty rectangle. The pointer is borrowed and valid only during the callback.

The UWP host converts only the dirty pixels, merges pending rectangles, copies only the merged rectangle into the render snapshot, and uploads it with a `D3D11_BOX`. A surface switch, texture recreation, or dimension change forces one complete-frame update. DLLs without the API 1.2 export continue through `qemu_host_register_video_callback` and send complete frames.

## Blocking event loop in API 1.3

API 1.3 adds the optional, thread-safe `qemu_host_wake_main_loop` export. When it is present, the app calls `qemu_host_main_loop_step(false, ...)` so QEMU sleeps until a timer, device or host event needs service. Input and lifecycle queues call the wake export after adding work.

This removes continuous host-side polling that could otherwise consume CPU time needed by TCG vCPU threads. DLLs without the API 1.3 export continue to use the legacy nonblocking loop with cooperative yields.

API 1.3 also provides `qemu_host_pause`, `qemu_host_resume`, and `qemu_host_request_stop`. Pause and Resume change the QEMU runstate while holding its main-loop locks. Stop forces an embedded-session exit even when the guest profile uses `-no-shutdown`. The separate `qemu_host_request_shutdown` action delivers the emulated power-button event so an ACPI-aware guest can shut down cleanly; the UI keeps Stop available if the guest does not respond.
