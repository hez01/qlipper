# Qlipper (hez01 fork)

Lightweight, cross-platform clipboard history applet that lives in the system
tray and lets you pick a previous entry from a searchable, live-filtered menu.

This is a fork of [pvanek/qlipper](https://github.com/pvanek/qlipper) with added
features. All upstream credit belongs to
Petr Vanek and the original contributors.

## What this fork adds

- **Image history with thumbnails.** Copied images are kept in history and shown
  as thumbnails, backed by a content-addressed on-disk cache so identical copies
  are stored once.
- **SQLite-backed storage.** History is stored in SQLite, so each insert, move-to-top,
  trim, and per-entry delete is a single targeted statement instead of rewriting the
  whole history on every change. No more hanging when there's a lot of large entries.
- **Unlimited-history option** and **per-entry deletion** with the Delete key.
- **Larger thumbnails and a scrollable history menu.** Entries render at a larger
  icon size, and a long history scrolls.
- **Search.** Includes search for quickly finding items in your unlimited history.

## Building (tried on Ubuntu 24.04)

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

## Platforms

Only tested on Lubuntu 24.04, Xorg, but should work on other platforms (check original qlipper).

## Credits and license

Original author: Petr Vanek `<petr@yarpen.cz>`. Upstream:
[github.com/pvanek/qlipper](https://github.com/pvanek/qlipper).

License: **GPLv2+** (see [COPYING](COPYING)). This fork is distributed under the
same terms.
