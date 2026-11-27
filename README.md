# Crystals

<p align="left">
  <a href="https://github.com/CuarzoSoftware/Crystals/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/license-LGPLv2.1-blue.svg" alt="Crystals is released under the LGPLv2.1 license." />
  </a>
</p>

> This project is part of the [Cuarzo Framework](https://github.com/CuarzoSoftware/Cuarzo). Follow the instructions there to build and install it.

A macOS-like desktop for Linux. Crystals is the Wayland compositor of Cuarzo OS, built on [Louvre](https://github.com/CuarzoSoftware/Louvre). It brings the whole desktop together: it launches the shell (menu bar, dock and desktop) and draws windows with their decorations, shadows, blur and animations.

![Desktop](https://i.imgur.com/ub1aQvo.jpeg)

## Features

- **Blur Effects:** translucent windows, menus, the menu bar and the dock blur what's behind them, as clients request through the background blur protocol.
- **Exposé/Workspaces:** see every window of the workspace at once, and move between workspaces. Use a three-finger swipe up or Ctrl+Super+Up to show the windows, and a sideways three-finger swipe or Ctrl+Super+Left/Right to change workspace. Fullscreen windows get a workspace of their own.
- **Show Desktop:** move every window aside, faded and almost off the screen, to use the desktop. Use a three-finger swipe down or Super+D. Swipe up, press Super+D again, click a window, drag something over a window or change workspace to bring them back.
- **Finger Swipe Gestures:** touchpad gestures follow the fingers and finish with springy animations.
- **Desktop Icons:** files of the desktop folder and connected disks, with thumbnails, drag and drop and rubber band selection ([Desk](https://github.com/CuarzoSoftware/Desk)).
- **Dock:** an auto-hiding dock with pinned and running apps, minimized windows, launch bounces and a trash ([Shelf](https://github.com/CuarzoSoftware/Shelf)).
- **Global Menu Bar:** the menus of the active app at the top of the screen ([Firmament](https://github.com/CuarzoSoftware/Firmament) and [Heaven](https://github.com/CuarzoSoftware/Heaven)).
- **Settings App:** System Settings with a sidebar and searchable sections, like macOS's ([Settings](https://github.com/CuarzoSoftware/Settings)).
- **Control Panel:** quick toggles (Wi-Fi, Dark Mode, ...), Wi-Fi networks, volume and sound devices, and Now Playing, from the menu bar.
- **Color Scheme:** light and dark modes, switched instantly across the desktop and its apps.

Crystals also supports screen sharing ([xdg-desktop-portal-cz](https://github.com/CuarzoSoftware/xdg-desktop-portal-cz)), a lock screen ([Doorman](https://github.com/CuarzoSoftware/Doorman)) and screenshots: press Print Screen or Super+Shift+3 to save one to the desktop.

## Light Mode

![Light Mode](https://i.imgur.com/GeI1Yzw.jpeg)

## Dark Mode

![Dark Mode](https://i.imgur.com/ZaFOiph.jpeg)

## Exposé/Workspaces

![Exposé/Workspaces](https://i.imgur.com/Tpgwhjs.jpeg)

## Keyboard Shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+Super+Up/Down | Show/hide Exposé |
| Ctrl+Super+Left/Right | Previous/next workspace |
| Super+D | Show/hide the desktop |
| Super+Space | Next keyboard layout (with several) |
| Print Screen, Super+Shift+3 | Screenshot to the desktop |

## License

Released under the GNU Lesser General Public License v2.1. See [LICENSE](LICENSE).
