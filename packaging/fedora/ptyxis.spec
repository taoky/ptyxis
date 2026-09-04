Name:           ptyxis
Version:        %{?snapshot_version}%{!?snapshot_version:50.2}
Release:        %{?snapshot_release}%{!?snapshot_release:1}%{?dist}
Summary:        Container-oriented terminal for GNOME

License:        GPL-2.0-or-later AND GPL-3.0-or-later AND LGPL-2.0-or-later AND LGPL-3.0-or-later AND CC0-1.0
URL:            https://gitlab.gnome.org/GNOME/ptyxis
Source0:        %{name}-%{version}.tar.xz

BuildRequires:  gcc
BuildRequires:  gettext
BuildRequires:  meson >= 1.0
BuildRequires:  pkgconfig(gio-unix-2.0) >= 2.80
BuildRequires:  pkgconfig(gtk4) >= 4.14
BuildRequires:  pkgconfig(json-glib-1.0) >= 1.6
BuildRequires:  pkgconfig(libadwaita-1) >= 1.8
BuildRequires:  pkgconfig(libportal-gtk4)
BuildRequires:  pkgconfig(vte-2.91-gtk4) >= 0.79
BuildRequires:  /usr/bin/appstreamcli
BuildRequires:  /usr/bin/desktop-file-validate

Suggests:       distrobox
Suggests:       podman

%description
Ptyxis is a terminal emulator designed for GNOME. It provides first-class
support for containers through Podman, Toolbx, and Distrobox, along with
profiles, searchable tabs, and split terminal panes.

%prep
%autosetup -p1

%build
%meson --buildtype=plain --wrap-mode=nodownload \
       -Dpackage-version=%{version}-%{release}
%meson_build

%install
%meson_install
%find_lang %{name}

%check
%meson_test

%files -f %{name}.lang
%license COPYING
%doc NEWS README.md docs/PACKAGING.md
%{_bindir}/ptyxis
%{_bindir}/ptyxis-quake-daemon
%{_libexecdir}/ptyxis-agent
%{_mandir}/man1/ptyxis.1*
%{_datadir}/applications/org.gnome.Ptyxis.desktop
%{_datadir}/dbus-1/services/org.gnome.Ptyxis.service
%{_datadir}/dbus-1/services/org.gnome.Ptyxis.QuakeDaemon.service
%{_datadir}/glib-2.0/schemas/org.gnome.Ptyxis.gschema.xml
%{_datadir}/icons/hicolor/*/apps/org.gnome.Ptyxis*.svg
%{_datadir}/metainfo/org.gnome.Ptyxis.metainfo.xml
%{_datadir}/ptyxis/org.gnome.Ptyxis.QuakeDaemon.desktop

%changelog
* Fri Aug 21 2026 Ptyxis snapshot builder <noreply@example.invalid> - 50.2-1
- Package the current upstream release
