# Gamepad input

Qemu-UWP-host supports Xbox-compatible controllers through `Windows.Gaming.Input.Gamepad`.
The app polls the first connected gamepad and converts controller state into the same QEMU input path used by keyboard and mouse capture.

## API used

The implementation uses:

- `Windows.Gaming.Input.Gamepad::Gamepads` to get the connected controllers.
- `Gamepad::GetCurrentReading()` to poll the current controller state.
- `GamepadReading.LeftThumbstickX` and `GamepadReading.LeftThumbstickY` for mouse movement.
- `GamepadReading.LeftTrigger` and `GamepadReading.RightTrigger` for the virtual keyboard toggle.
- `GamepadReading.Buttons` with `GamepadButtons` flags for D-pad, face buttons, and bumpers.

## Input modes

There are two controller modes.

## Emulator control mode

This is the normal mode when the virtual keyboard overlay is hidden.

| Controller input | Action |
| --- | --- |
| Left analog stick | Moves the guest mouse pointer |
| Directional Pad Up | Sends QEMU Up arrow |
| Directional Pad Down | Sends QEMU Down arrow |
| Directional Pad Left | Sends QEMU Left arrow |
| Directional Pad Right | Sends QEMU Right arrow |
| A Button | Sends QEMU Enter |
| X Button | Holds/releases the guest left mouse button |
| Y Button | Holds/releases the guest right mouse button |
| Left Trigger + Right Trigger | Shows or hides the virtual keyboard |
| Left Bumper + Right Bumper | Toggles mouse and keyboard capture, same as `Ctrl+Alt+M` |

The left analog stick uses a deadzone of `0.8`. Values inside that range are ignored. Values outside the deadzone are normalized and use light acceleration so stronger stick movement produces faster pointer movement.

The D-pad and A button are state-based: the app sends key down when the button is pressed and key up when it is released. This avoids flooding QEMU with repeated key presses every polling tick.

X and Y are also state-based in emulator control mode. Holding X holds the left mouse button; holding Y holds the right mouse button. Releasing each button releases the matching mouse button in the guest.

## Virtual keyboard mode

This mode is active while the virtual keyboard overlay is visible.

| Controller input | Action |
| --- | --- |
| Directional Pad | Moves the selected key in the virtual keyboard |
| X Button | Presses the selected virtual key |
| Y Button | Closes the virtual keyboard |
| Left Trigger + Right Trigger | Shows or hides the virtual keyboard |
| Left Bumper + Right Bumper | Toggles mouse and keyboard capture, same as `Ctrl+Alt+M` |

While the virtual keyboard is open, D-pad and A button input are not sent directly to the guest. This prevents navigation inside the overlay from also moving selection or focus inside the emulated system.

## Virtual keyboard behavior

The virtual keyboard is a transparent overlay above the emulator video. It is controlled entirely by the gamepad and does not take pointer or keyboard focus from the app UI.

The overlay includes common PC keys:

- Escape, F1-F12, Print Screen, Scroll Lock, Pause
- Number row and punctuation
- Tab, Caps Lock, Shift, Ctrl, Alt, AltGr
- Enter, Backspace, Space
- Insert, Delete, Home, End, Page Up, Page Down
- Arrow keys
- Letter keys

When a virtual key is pressed with X, the app sends a short QEMU key down followed by key up for the selected key.

## Capture behavior

`Left Bumper + Right Bumper` mirrors the existing `Ctrl+Alt+M` capture shortcut.

When capture is enabled:

- Mouse and keyboard input go to the emulator.
- Gamepad emulator control mode is active.
- The virtual keyboard can be opened with both triggers.

When capture is disabled:

- Guest keys and controller-held states are released.
- The virtual keyboard is hidden.
- The bumper shortcut can still be used to recapture input.

When RFB mode is enabled, local mouse and keyboard capture is disabled and controller input is not sent to the guest. Input should come from the RFB/VNC client instead.

## Implementation locations

Main files:

- `DirectXPage.xaml`: virtual keyboard overlay layout.
- `DirectXPage.xaml.h`: gamepad state, virtual keyboard state, and helper declarations.
- `DirectXPage.xaml.cpp`: gamepad polling, button state transitions, analog stick handling, capture toggle, and virtual keyboard behavior.
