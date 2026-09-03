# Packaging Ptyxis

This repository contains reproducible packaging definitions for the current
Git revision. They produce native packages for Debian, Ubuntu, Fedora, and
Arch Linux, plus a Flatpak bundle.

## Supported targets

| Target | Build environment | Artifact |
|---|---|---|
| Debian | Debian sid | `.deb` and `-dbgsym.deb` |
| Ubuntu | Ubuntu 26.04 | `.deb` and `-dbgsym.deb` |
| Fedora | Fedora 44 | `.rpm` |
| Arch Linux | current rolling image | `.pkg.tar.zst` |
| Flatpak | GNOME 50 runtime | application and `.Debug` extension bundles |

The current source requires GLib 2.80, GTK 4.14, libadwaita 1.8, JSON-GLib
1.6, VTE 0.79, and the GTK 4 libportal backend. Debian 13 does not provide the
complete dependency set at these versions. Debian 13 users should use the
Flatpak; the native Debian package intentionally targets sid rather than
vendoring core GNOME libraries.

Native packages retain the normal `ptyxis` package name and
`org.gnome.Ptyxis` application ID. The Flatpak also uses
`org.gnome.Ptyxis`, so it is separate from the historical Flathub application
ID `app.devsuite.Ptyxis`.

## Reproducible source and versions

All targets are built from `git archive HEAD`. Tracked modifications staged or
unstaged cause the build to stop. Untracked files, including `PLAN.md`, are not
part of the archive and do not affect the package.

When `HEAD` is exactly at a tag, the script uses that tag as the package
version (with an optional leading `v` removed). For example, tag `50.2`
produces Debian `50.2-1`, Ubuntu `50.2-1~ubuntu26.04.1`, Fedora `50.2-1.fc44`,
Arch `50.2-1`, and `ptyxis-50.2-x86_64.flatpak`.

For commits without an exact tag, the script reads the base version from
`meson.build` and derives snapshot versions from the commit timestamp and
short commit ID. For example:

- Debian: `50.2+git20260821.28c8c32-1`
- Ubuntu: `50.2+git20260821.28c8c32-1~ubuntu26.04.1`
- Fedora: `50.2-0.git20260821.28c8c32.fc44`
- Arch: `50.2.r85.g28c8c32-1`

Builds use the host CPU architecture. Run the same workflow on an arm64 host
to produce arm64/aarch64 packages; no QEMU or Docker buildx emulation is
configured.

## Build commands

Native builds require Docker or Podman. The script uses Docker when both are
installed and otherwise falls back to Podman. Set `CONTAINER_ENGINE` to select
one explicitly, for example:

```sh
CONTAINER_ENGINE=podman ./packaging/build-packages.sh fedora
```

Flatpak builds require `flatpak` and
`flatpak-builder` on the host.

Build one target:

```sh
./packaging/build-packages.sh debian
./packaging/build-packages.sh ubuntu
./packaging/build-packages.sh fedora
./packaging/build-packages.sh arch
./packaging/build-packages.sh flatpak
```

Build every target sequentially:

```sh
./packaging/build-packages.sh all
```

Native artifacts are written below a versioned directory such as
`dist/fedora/git20260821.28c8c32/`. Flatpak output is written to
`dist/flatpak/`, and its local OSTree repository is retained at
`dist/flatpak/repo/`. Debian and Ubuntu builds include a `ptyxis-dbgsym`
package. Flatpak builds include a separate `ptyxis-debug-*.flatpak` bundle
containing the `org.gnome.Ptyxis.Debug` extension.

The containers download build dependencies from their distribution mirrors.
Flatpak requires Flathub for the GNOME SDK and runtime:

```sh
flatpak remote-add --if-not-exists flathub \
  https://dl.flathub.org/repo/flathub.flatpakrepo
```

The generated bundles and native packages are unsigned. Signing and repository
publication are deliberately outside this local-build workflow.

## Packaging layout

- `debian/` is a complete debhelper package definition shared by Debian and
  Ubuntu. The container updates a temporary copy of `debian/changelog` with
  the generated snapshot version.
- `packaging/fedora/ptyxis.spec` is the RPM definition. The container build
  passes the snapshot version and release as RPM macros.
- `packaging/arch/PKGBUILD.in` is rendered in the container with the generated
  version and source archive checksum before `makepkg` runs.
- `packaging/flatpak/org.gnome.Ptyxis.json` pins third-party sources and builds
  the clean archived checkout with the GNOME 50 SDK.
- `packaging/Dockerfile` contains separate build and artifact-export stages for
  the four native targets and is consumed by either Docker or Podman.

The native builds use distribution libraries with Meson's
`--wrap-mode=nodownload`; missing or outdated dependencies fail rather than
being silently downloaded. The install must always contain both
`/usr/bin/ptyxis` and `/usr/libexec/ptyxis-agent`.

## Installing local artifacts

Debian or Ubuntu:

```sh
sudo apt install ./dist/debian/gitYYYYMMDD.SHA/ptyxis_*.deb
```

Use `dist/ubuntu/` instead for an Ubuntu build.
Install the matching `ptyxis-dbgsym_*.deb` when collecting a backtrace.

Fedora:

```sh
sudo dnf install ./dist/fedora/gitYYYYMMDD.SHA/ptyxis-*.rpm
```

Arch Linux:

```sh
sudo pacman -U ./dist/arch/gitYYYYMMDD.SHA/ptyxis-*.pkg.tar.zst
```

Flatpak:

```sh
flatpak install --user ./dist/flatpak/ptyxis-[0-9]*.flatpak
flatpak run org.gnome.Ptyxis
```

To collect a backtrace, install both the application bundle and its matching
debug extension:

```sh
flatpak install --user ./dist/flatpak/ptyxis-debug-*.flatpak
```

The extension is selected automatically when debugging the application.

Because these packages use the official native package name and application
ID, installing them can replace a distribution-provided Ptyxis build. The
package manager may upgrade or downgrade it again when repositories change.

## Verification

Each native package build runs the upstream Meson test suite in its target
container. After installation, verify:

1. `ptyxis --version` and `ptyxis --help` work without missing libraries.
2. The package contains `ptyxis-agent` and a terminal session starts.
3. Desktop and D-Bus activation work.
4. GSettings schemas, AppStream metadata, icons, translations, and the manual
   page are installed.
5. The application starts in both Wayland and X11 sessions where available.
6. Podman, Toolbx, or Distrobox containers are discovered when their host
   tools are installed.

Inspect packages without installing them:

```sh
dpkg-deb --info ptyxis_*.deb
dpkg-deb --contents ptyxis_*.deb
rpm -qip ptyxis-*.rpm
rpm -qlp ptyxis-*.rpm
bsdtar -tf ptyxis-*.pkg.tar.zst
flatpak info --show-runtime org.gnome.Ptyxis
```

The last Flatpak command applies after installing the bundle. GUI and host
container integration cannot be fully tested during a headless package build.
