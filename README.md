# Qlipper (hez01 fork)

Lightweight, cross-platform clipboard history applet that lives in the system
tray and lets you pick a previous entry from a searchable, live-filtered menu.

This is a fork of [pvanek/qlipper](https://github.com/pvanek/qlipper) with added
features and Ubuntu 24.04 build support. All upstream credit belongs to
Petr Vanek and the original contributors.

## What this fork adds

- **Image history with thumbnails.** Copied images are kept in history and shown
  as thumbnails, backed by a content-addressed on-disk cache so identical copies
  are stored once.
- **SQLite-backed storage.** History is stored in SQLite, so each insert, move-to-top,
  trim, and per-entry delete is a single targeted statement instead of rewriting the
  whole history on every change.
- **Unlimited-history option** and **per-entry deletion** with the Delete key.
- **Larger thumbnails and a scrollable history menu.** Entries render at a larger
  icon size, and a long history scrolls instead of overflowing off-screen.
- **Builds on Ubuntu 24.04.** KF6 `KSystemClipboard` is now optional: when the
  KF6 GuiAddons framework is not present (as on stock Ubuntu 24.04) the build falls
  back to `QClipboard`. Debian packaging is included.

## Building on Ubuntu 24.04

Install the build dependencies:

```sh
sudo apt install cmake g++ debhelper \
  qt6-base-dev qt6-base-private-dev qt6-l10n-tools qt6-tools-dev \
  libx11-dev desktop-file-utils libqt6sql6-sqlite
```

Build a `.deb` package:

```sh
dpkg-buildpackage -us -uc -b
sudo apt install ../qlipper_*_amd64.deb
```

Or build directly with CMake:

```sh
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
make
sudo make install
```

If the KF6 GuiAddons framework is installed, the build uses `KSystemClipboard`
for full clipboard monitoring under Wayland; otherwise it falls back to
`QClipboard`, which on Wayland only sees the clipboard while Qlipper has focus.
Clipboard monitoring works fully on X11 in both cases.

## Platforms

Linux, BSD, Windows, and macOS. The Ubuntu 24.04 notes above are specific to this
fork's packaging; see upstream for other platforms.

## Credits and license

Original author: Petr Vanek `<petr@yarpen.cz>`. Upstream:
[github.com/pvanek/qlipper](https://github.com/pvanek/qlipper).

License: **GPLv2+** (see [COPYING](COPYING)). This fork is distributed under the
same terms.
