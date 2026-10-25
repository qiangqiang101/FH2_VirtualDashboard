# VirtualDashboard v1.1.0

A mod for Forza Horizon 2 (Azimuth 1.0.0.64) that serves a live dashboard on your local network. Open it on your phone or tablet to see speed, RPM, gear and more while you drive.

## Features

- Speedometer (tap to switch km/h and mph) with the scale adjusting to your speed.
- RPM gauge with the redline read from the current car. The digital RPM is rounded to 50 by default; tap the gauge to toggle.
- Gear (tap it to swap with the speed), headlights icon, and turn-signal and hazard arrows (blinking like the game).
- Car name, plus a gauge sweep animation on every car change.
- A hidden list of raw telemetry values (arrow button): speed, rpm, gear, throttle, brake, clutch, handbrake, turbo boost, approximate power and torque, game time and weather, with a footer under it.
- Keeps showing live data in game menus (including the in-game Telemetry screen).
- Works in portrait and landscape.

## Requirements

- Forza Horizon 2 running through Azimuth, version 1.0.0.64.
- [FH2 Mod Platform](https://github.com/Trinculo54/FH2-Mod-Platform) v2.7.1 or newer (API 7+).
- Extra Options (`winmm.dll`) is needed only for the turn-signal and hazard controls.
- PC and phone on the same Wi-Fi or LAN.

## Installation

1. Close the game.
2. Copy the `fh2-mods` folder from this package into the folder that contains your FH2 Mod Platform. Allow merging. This places `VirtualDashboard.dll` and `VirtualDashboard.ini` in your existing `fh2-mods` folder.
3. Start the game. The log (`fh2-mods\platform.log`) should show `[VirtualDashboard] 1.1.0 registered`.
4. Allow `azimuth` through Windows Firewall for **private** networks when prompted. If the prompt didn't appear, allow it manually in Windows Defender Firewall.
5. Find your PC's IPv4 address (`ipconfig`) and open `http://<PC-IP>:8080/` on your phone, for example `http://192.168.178.85:8080/`.

## Settings

- **In game:** Mods page, "Phone dashboard", turns the server on or off.
- **`fh2-mods\VirtualDashboard.ini`:**
  ```
  [VirtualDashboard]
  Enabled=1
  Port=8080
  ```
  Change `Port` if 8080 is taken. Restart the game after changing it.

## Notes

- The dashboard keeps working in menus. It shows "Waiting for a car in the game..." during loading screens, after a car swap, or when the connection is lost.
- Power and torque are approximations and may differ from the in-game Telemetry page. Boost is shown in bar.
- Indicators are driven by the D-pad as Extra Options does: hold left/right for 1 second, tap up for hazards. They are mirrored by the mod rather than read from the game, so they can differ in odd cases. Work in progress to adress this issue and implement better solution.
- A JSON feed is available at `http://<PC-IP>:8080/api`.
- The server has no password and is meant for a trusted home network. Don't expose the port to the internet.

## Uninstall

Delete `VirtualDashboard.dll` and `VirtualDashboard.ini` from `fh2-mods`.

Created by suderek. [Visit FH2mods](https://fh2mods.com/)
