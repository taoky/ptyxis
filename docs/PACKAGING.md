# Packaging Ptyxis

This document is intended for distribution and downstream package maintainers.
It covers native packages for Debian, Fedora, and Arch Linux, and development
builds using Flatpak. End users should normally install the package provided by
their distribution or the stable Flatpak from Flathub.

## Build requirements

The authoritative dependency versions are in the top-level `meson.build`.
At the time of writing, Ptyxis requires:

- Meson 1.0 or newer and Ninja
- a C compiler and `pkg-config`
- GLib/GIO 2.80 or newer
- GTK 4.14 or newer
- libadwaita 1.8 or newer
- JSON-GLib 1.6 or newer
- VTE for GTK 4, version 0.79 or newer
- libportal with its GTK 4 backend on Linux
- gettext

`desktop-file-utils` and AppStream are optional at configure time, but should
be installed when running the complete test suite. Native packages should also
declare the corresponding runtime libraries according to their distribution's
dependency generation policy.

Some stable distribution releases do not contain versions new enough to build
the current development branch. Do not disable Meson's version checks. Package
a compatible Ptyxis release, use a newer distribution suite, or use the GNOME
Flatpak SDK instead.

For a release build from an unpacked source archive:

```sh
meson setup _build \
  --prefix=/usr \
  --buildtype=plain \
  --wrap-mode=nodownload
meson compile -C _build
meson test -C _build --print-errorlogs
DESTDIR=/path/to/package-root meson install -C _build
```

Distribution build flags should be supplied through `CFLAGS`, `CPPFLAGS`, and
`LDFLAGS`. `--wrap-mode=nodownload` ensures that missing system dependencies
fail the build instead of being downloaded through a Meson wrap.

The install contains both `/usr/bin/ptyxis` and `/usr/libexec/ptyxis-agent`.
They must be shipped together. The agent is how Ptyxis creates and monitors
PTYs, including when the UI runs in a Flatpak.

## Debian and Ubuntu

The following package names are suitable for current Debian-family suites that
meet the minimum versions above:

```sh
sudo apt install \
  build-essential debhelper dh-sequence-gnome meson ninja-build \
  pkg-config gettext appstream desktop-file-utils \
  libglib2.0-dev libgtk-4-dev libadwaita-1-dev libjson-glib-dev \
  libvte-2.91-gtk4-dev libportal-gtk4-dev
```

Ptyxis already has official Debian packaging in the
[GNOME Team packaging repository][debian-packaging]. When updating that
package, prefer the existing packaging over creating a second `debian/`
directory from this example.

A minimal source package uses debhelper's Meson build system. Its
`debian/control` build dependencies should include:

```debcontrol
Build-Depends:
 debhelper-compat (= 13),
 dh-sequence-gnome,
 meson (>= 1.0),
 pkgconf,
 libglib2.0-dev (>= 2.80),
 libgtk-4-dev (>= 4.14),
 libadwaita-1-dev (>= 1.8),
 libjson-glib-dev (>= 1.6),
 libvte-2.91-gtk4-dev (>= 0.79),
 libportal-gtk4-dev,
 appstream,
 desktop-file-utils,
 gettext
```

A typical `debian/rules` is:

```make
#!/usr/bin/make -f

%:
	dh $@ --buildsystem=meson --with gnome

override_dh_auto_configure:
	dh_auto_configure -- --buildtype=plain --wrap-mode=nodownload
```

Build the source and binary packages with:

```sh
dpkg-buildpackage -b -uc -us
```

Use `dpkg-shlibdeps` through debhelper rather than manually listing shared
library ABI packages. Ensure the resulting binary package includes the
executable, agent, D-Bus service, GSettings schemas, desktop file, AppStream
metadata, icons, translations, and manual page.

## Fedora

Install the native build dependencies with:

```sh
sudo dnf install \
  gcc meson ninja-build pkgconf-pkg-config gettext appstream \
  desktop-file-utils glib2-devel gtk4-devel libadwaita-devel \
  json-glib-devel vte291-gtk4-devel libportal-gtk4-devel
```

Fedora ships an [official Ptyxis package][fedora-package]. Its spec file is the
best starting point for a Fedora update. A minimal spec uses these build
requirements and Meson macros:

```spec
BuildRequires: gcc
BuildRequires: meson >= 1.0
BuildRequires: gettext
BuildRequires: appstream
BuildRequires: desktop-file-utils
BuildRequires: pkgconfig(gio-unix-2.0) >= 2.80
BuildRequires: pkgconfig(gtk4) >= 4.14
BuildRequires: pkgconfig(libadwaita-1) >= 1.8
BuildRequires: pkgconfig(json-glib-1.0) >= 1.6
BuildRequires: pkgconfig(vte-2.91-gtk4) >= 0.79
BuildRequires: pkgconfig(libportal-gtk4)

%build
%meson --buildtype=plain --wrap-mode=nodownload
%meson_build

%install
%meson_install

%check
%meson_test
```

Build locally with Fedora's standard RPM tooling:

```sh
rpmbuild -ba ptyxis.spec
```

For reproducible clean builds, use Mock with a target matching the Fedora
release being packaged:

```sh
mock -r fedora-rawhide-x86_64 --rebuild ptyxis.src.rpm
```

Let RPM generate shared-library dependencies. Run
`desktop-file-validate` and `appstreamcli validate --no-net` in `%check` if the
standard Meson tests are not executed there.

## Arch Linux

On an up-to-date Arch Linux system, install the build dependencies with:

```sh
sudo pacman -S --needed \
  base-devel meson ninja pkgconf gettext appstream desktop-file-utils \
  glib2-devel gtk4 libadwaita json-glib vte4 libportal-gtk4
```

Ptyxis is available in Arch's official repositories. Consult the
[official package][arch-package] before maintaining a derivative PKGBUILD.
A compact PKGBUILD skeleton is:

```bash
pkgname=ptyxis
pkgver=50.2
pkgrel=1
pkgdesc='Container-oriented terminal for GNOME'
arch=(x86_64 aarch64)
url='https://gitlab.gnome.org/GNOME/ptyxis'
license=(GPL-3.0-or-later)
depends=(glib2 gtk4 libadwaita json-glib vte4 libportal-gtk4)
makedepends=(git meson ninja pkgconf gettext appstream desktop-file-utils)
source=("https://download.gnome.org/sources/ptyxis/${pkgver%%.*}/ptyxis-${pkgver}.tar.xz")
sha256sums=('REPLACE_WITH_RELEASE_ARCHIVE_SHA256')

build() {
  arch-meson "$pkgname-$pkgver" build \
    --buildtype=plain \
    --wrap-mode=nodownload
  meson compile -C build
}

check() {
  meson test -C build --print-errorlogs
}

package() {
  meson install -C build --destdir "$pkgdir"
}
```

Update the version, source URL, checksum, architectures, license expression,
and dependency lists for the release being packaged. Then build in a clean
chroot rather than relying only on the maintainer's workstation:

```sh
extra-x86_64-build
```

For a quick local check, `makepkg -sCcf` is sufficient, but it is not a
replacement for the clean-chroot build used for repository packages.

## Flatpak

The repository contains `org.gnome.Ptyxis.Devel.json`, which targets the GNOME
development runtime and builds libportal and VTE before Ptyxis. Install the
Flatpak tooling and configure Flathub:

```sh
flatpak remote-add --if-not-exists flathub \
  https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.flatpak.Builder
```

Build and install the development package with:

```sh
flatpak run org.flatpak.Builder \
  --user \
  --install \
  --force-clean \
  --install-deps-from=flathub \
  _flatpak-build \
  org.gnome.Ptyxis.Devel.json
flatpak run org.gnome.Ptyxis.Devel
```

The checked-in manifest follows upstream development branches. It therefore
builds upstream sources, not uncommitted changes in the current checkout. To
package a fork or release, change the final `ptyxis` module source to one of:

- a `dir` source pointing at a clean local checkout for development; or
- a release archive with a fixed URL and SHA-256 checksum; or
- a Git source pinned to an immutable commit.

Release manifests must also pin every dependency source. Do not publish a
manifest that follows `main` or `master`, because the same manifest could
produce different binaries later.

The development manifest uses the application ID `org.gnome.Ptyxis.Devel` and
passes `-Ddevelopment=true -Dlibc-compat=true`. The stable Flathub application
currently uses `app.devsuite.Ptyxis`; do not change an established application
ID during an update, because Flatpak treats a different ID as a different
application.

To export a local OSTree repository and bundle instead of installing directly:

```sh
flatpak run org.flatpak.Builder \
  --user \
  --force-clean \
  --install-deps-from=flathub \
  --repo=_flatpak-repo \
  _flatpak-build \
  org.gnome.Ptyxis.Devel.json

flatpak build-bundle \
  _flatpak-repo \
  ptyxis-devel.flatpak \
  org.gnome.Ptyxis.Devel
```

See the Flatpak documentation on [manifests][flatpak-manifests] and
[`flatpak-builder`][flatpak-builder] for signing and repository publication.

## Package verification

Test the package in a clean environment, not only from the source build tree.
At minimum, verify:

1. `ptyxis --version` and `ptyxis --help` run without missing libraries.
2. The desktop launcher starts the application under both Wayland and X11.
3. A shell starts successfully, proving that `ptyxis-agent` was packaged.
4. GSettings schemas are visible and preferences persist after restart.
5. Icons, AppStream metadata, translations, D-Bus activation, and the
   `ptyxis(1)` manual page are installed.
6. Podman, Toolbx, or Distrobox containers are discovered when the relevant
   host tools are installed.
7. `meson test -C _build --print-errorlogs` passes in the package build.
8. No files are installed outside the package staging root.

[debian-packaging]: https://salsa.debian.org/gnome-team/ptyxis
[fedora-package]: https://packages.fedoraproject.org/pkgs/ptyxis/ptyxis/
[arch-package]: https://archlinux.org/packages/extra/x86_64/ptyxis/
[flatpak-manifests]: https://docs.flatpak.org/en/latest/manifests.html
[flatpak-builder]: https://docs.flatpak.org/en/latest/building-introduction.html
