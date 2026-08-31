![Ptyxis Logo](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/icons/ptyxis.svg)

[![License: GPL v3+](https://img.shields.io/badge/License-GPL%20v3%2B-blue.svg)](https://gitlab.gnome.org/chergert/ptyxis/-/blob/main/COPYING)
[![GitLab Pipeline Status](https://gitlab.gnome.org/chergert/ptyxis/badges/main/pipeline.svg)](https://gitlab.gnome.org/chergert/ptyxis/-/pipelines)
[![Distribution: Flatpak (Stable)](https://img.shields.io/badge/Available%20on-Flatpak%20(Stable)-4d9c9a.svg?logo=flatpak&logoColor=white)](https://flathub.org/apps/app.devsuite.Ptyxis)
[![Distribution: Flatpak (Nightly)](https://img.shields.io/badge/Available%20on-Flatpak%20(Nightly)-4d9c9a.svg?logo=flatpak&logoColor=white)](https://gitlab.gnome.org/chergert/ptyxis#flatpak-recommended)

# Ptyxis: Your Container-Oriented Terminal for GNOME

**A modern terminal emulator built for the container era.**
_Seamlessly navigate between your host system and local containers like Podman,
Toolbox, and Distrobox with intelligent detection and a beautiful, responsive
GNOME interface._

![Ptyxis Screenshot - Tab Overview](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/tab-overview.png)
_Fig 1: Ptyxis tab overview for managing multiple terminal sessions._

---

- [What Makes Ptyxis Special?](#what-makes-ptyxis-special)
- [Key Features](#key-features)
- [Screenshots](#screenshots)
- [Installation](#installation)
- [Basic Usage & Command-Line Options](#basic-usage--command-line-options)
- [Container Integration](#container-integration)
- [Contributing: Feature Requests and Design Changes](#contributing-feature-requests-and-design-changes)
- [Report an Issue](https://gitlab.gnome.org/chergert/ptyxis/-/issues)

---

## What Makes Ptyxis Special?

Ptyxis (the application project was formerly named "Prompt" and may still appear
under `app.devsuite.Ptyxis` for its stable Flatpak release) isn't just another
terminal emulator—it's engineered from the ground up for modern development
workflows within the GNOME desktop, where local containers are first-class
citizens. It simplifies and enhances your interaction with tools like Podman,
Toolbox, and Distrobox, making them a natural extension of your terminal
experience. Ptyxis is the default terminal in Fedora Workstation,
Red Hat Enterprise Linux, and Ubuntu, highlighting its robust design
and performance.

## Key Features

- **First-Class Container Integration**: Automatic discovery, direct spawning,
  and context preservation (active container, CWD) for Podman, Toolbox,
  Distrobox, and JHBuild.
- **Modern GNOME Interface**: Built with GTK 4 and libadwaita for a native,
  responsive, and accessible user experience, adhering to GNOME HIG.
- **Dynamic Theming & Customization**: Extensive built-in color palettes that
  automatically adapt to system light/dark modes. Supports user-installable
  custom `.palette` files and "Window Dressing" for full-window theming.
- **Smart Process Tracking**: Visual indicators for `sudo` sessions, active
  SSH connections, and other foreground processes, enhancing situational
  awareness.
- **Advanced Tab Management**: Searchable tab overview with live previews and
  pinned tabs that persist their session context (profile, container,
  working directory) across application restarts.
- **Customizable Keyboard Shortcuts**: A comprehensive set of actions with
  highly configurable keyboard shortcuts accessible via the preferences.
- **Robust User Profiles**: Create named profiles to fine-tune default
  containers, startup commands, palette, terminal behaviors, and
  compatibility modes.
- **`ptyxis-agent` Architecture**: A unique out-of-process helper
  (`ptyxis-agent`) enables full functionality even when Ptyxis is run as a
  Flatpak by managing PTY creation, direct container interaction, and host
  process monitoring.
- **High-Performance Rendering**: Leverages the VTE (Virtual Terminal
  Emulator) library with GPU acceleration (Vulkan/OpenGL where available)
  for a remarkably fluid and responsive terminal experience.
- **Terminal Inspector**: An integrated developer tool for debugging
  terminal-based applications by allowing inspection of OSC (Operating System
  Command) hyperlinks, mouse event coordinates, and other terminal sequences.
- **Encrypted Scrollback Buffers**: Enhances privacy for your terminal
  session history.
- **Accessibility**: Designed with accessibility at its core, building upon
  GTK4 and VTE accessibility features to support screen readers and other
  assistive technologies effectively.

---

## Screenshots

Here's a glimpse into Ptyxis's interface and capabilities:

![Container Menu shows automatically discovered containers](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/containers-menu.png)
_Fig 2: Containers are automatically discovered and readily accessible._

![Tab Overview screen shows multiple tabs with previews](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/tab-overview.png)
_Fig 3: A visual, searchable overview of all open terminal tabs._

![Behavior Settings window for tweaking terminal behavior](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/change-behavior.png)
_Fig 4: Fine-tune various terminal behaviors to your preference._

![Palette Selector screen for choosing color schemes](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/palette-selector.png)
_Fig 5: Select from built-in color palettes or install your own._

![Profile Editor for customizing profiles](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/edit-profile.png)
_Fig 6: Profiles allow overriding features like default container._

![Shortcut Editor for remapping keyboard shortcuts](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/shortcut-editing.png)
_Fig 7: Remap keyboard shortcuts to fit your workflow._

![Sudo Tracking indicator in the terminal](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/sudo-tracking.png)
_Fig 8: The terminal clearly indicates when you're operating as root locally._

![SSH Tracking indicator in the terminal](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/ssh-tracking.png)
_Fig 9: A visual cue reminds you when connected to a remote system via SSH._

![Terminal with a partially transparent background](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/transparency.png)
_Fig 10: Optional background transparency for the daring._

![Terminal showing an overlay when the column or row size changes](https://gitlab.gnome.org/chergert/ptyxis/-/raw/main/data/screenshots/columns-and-rows.png)
_Fig 11: Column and row size indicator displayed during window resizing._

---

## Installation

### Flatpak (Recommended)

Ptyxis is primarily distributed as a Flatpak, ensuring all dependencies are
managed and providing a consistent experience across Linux distributions.

- **Stable Version (from Flathub):**
  The stable version uses the application ID `app.devsuite.Ptyxis`.

  ```bash
  flatpak install flathub app.devsuite.Ptyxis
  ```

  To run: `flatpak run app.devsuite.Ptyxis`

- **Nightly Development Version (from GNOME Nightly):**
  For the latest features (potentially less stable). This version uses the
  application ID `org.gnome.Ptyxis.Devel`.

  ```bash
  flatpak install --user --from https://nightly.gnome.org/repo/appstream/org.gnome.Ptyxis.Devel.flatpakref
  ```

  To run: `flatpak run org.gnome.Ptyxis.Devel`

### Building from Source

Ptyxis uses the Meson build system.

1. **Prerequisites:**
    Ensure your system has the necessary development packages installed:

    - C compiler (e.g., GCC, Clang)
    - Meson (version 1.0.0 or newer)
    - Ninja
    - GLib (version 2.80 or newer, e.g., `libglib2.0-dev`)
    - GTK4 (version 4.14 or newer, e.g., `libgtk-4-dev`)
    - libadwaita (version 1.6 or newer, e.g., `libadwaita-1-dev`)
    - JSON-GLib (version 1.6 or newer, e.g., `libjson-glib-dev`)
    - VTE (GTK4 version, 0.79 or newer, e.g., `libvte-2.91-gtk4-dev`)
    - libportal-gtk4 (on Linux, e.g., `libportal-gtk4-dev`)

2. **Clone the repository:**

    ```bash
    git clone https://gitlab.gnome.org/chergert/ptyxis.git
    cd ptyxis
    ```

3. **Configure the build using Meson:**

    ```bash
    meson setup _build --prefix=/usr/local --buildtype=release
    # Or --buildtype=debug for development
    ```

4. **Compile:**

    ```bash
    meson compile -C _build
    ```

5. **Install (optional):**

    ```bash
    sudo meson install -C _build
    ```

**Using GNOME Builder:**
[GNOME Builder](https://apps.gnome.org/Builder/) provides an excellent
integrated development environment. Simply open the cloned `ptyxis/` directory
in Builder for build configuration, running, and debugging.

Distribution maintainers should also read the
[packaging guide](docs/PACKAGING.md) for Debian, Fedora, Arch Linux, and
Flatpak-specific instructions.

---

## Basic Usage & Command-Line Options

Launch Ptyxis from your system's application launcher or by typing `ptyxis`
in an existing terminal (if installed to your PATH).

**Common Command-Line Options:**

- `--version`: Show the program version.
- `--preferences`: Open the preferences window directly.
- `--new-window`: Open a new Ptyxis window.
- `--toggle-quake`: Show or hide a persistent Quake-style terminal window.
  Bind this command to a global shortcut in your desktop environment.
- `--tab`: Open a new tab in the most-recently-used window.
- `--tab-with-profile=PROFILE_UUID`: Open a new tab using the specified
  profile UUID. (UUIDs can be copied from `Preferences -> Profiles`).
  Example: `ptyxis --tab-with-profile=a1b2c3d4-e5f6-7890-1234-567890abcdef`
- `-x "COMMAND"`, `--execute "COMMAND"`: Execute a command in a new,
  standalone window.
  Example: `ptyxis -x "htop"`
- `-- PROGRAM [ARGUMENTS...]`: Execute `PROGRAM` with `ARGUMENTS` in a new,
  standalone window.
  Example: `ptyxis -- htop -u myuser`
- `-d DIR`, `--working-directory DIR`: Set working directory for new
  tab/window or command.
  Example: `ptyxis -d /home/user/myproject`
- `--title=TITLE`: Set initial title for new tab/window.
- `--maximize`: Request new window starts maximized.
- `--fullscreen`: Request new window starts fullscreen.
- `--import-palette=FILE`: Import a Ptyxis `.palette` file.
- `-s`, `--standalone`: Start a new instance of Ptyxis.
- `-h`, `--help`: Show a summary of options.

For a complete list, refer to the `ptyxis(1)` man page.

---

## Core Functionality & Design

Ptyxis uses a two-part architecture for robust terminal emulation and deep
container integration within a GTK4/libadwaita UI.

### UI Application

The user-facing GTK4/libadwaita application manages:

- Windows, tabs, and the graphical interface.
- User profiles, theming (palettes, "Window Dressing"), and shortcuts.
- Displaying terminal content via the VTE widget.
- Communication with `ptyxis-agent`.

### `ptyxis-agent`

A distinct helper process, typically running on the host system (even when the
Ptyxis UI is a Flatpak). It handles:

- Creating and managing PTYs (pseudo-terminals).
- Direct interaction with container runtimes (Podman, Toolbox, etc.) for
  discovery and spawning processes within containers.
- Monitoring host processes and retrieving system info (shell, OS, proxies).
- D-Bus communication with the UI over a `socketpair()`.

This separation allows the UI to be responsive and potentially sandboxed, while
the agent handles lower-level system interactions.

---

## Container Integration

Ptyxis is engineered for intuitive integration with local containerized
development environments.

### Technical Details

1. **Discovery:** `ptyxis-agent` queries the host for containers (e.g.,
    `podman ps --all --format=json`) and monitors system files for dynamic
    updates to the container list in the UI.
2. **Spawning:** The agent constructs and executes CLI commands for the target
    runtime (e.g., `podman exec -it ...`, `toolbox enter ...`,
    `distrobox enter ...`). It manages PTY setup and I/O.
3. **Context Management:**
    - Active container identified by VTE termprops (e.g., `vte.container.name`).
    - CWD and select environment variables can be propagated into containers.
    - Profiles specify default container and inheritance behavior.
    - URI translation for "Open Link" from containers.

### `ptyxis-agent` Functions in Container Management

- Interfaces with container runtimes for discovery and execution.
- Monitors spawned processes (host and container), tracking foreground
  processes and reporting status/exit codes.
- Can create systemd user scopes for tabs for resource isolation (if
  `systemd-run` is available and new enough).

### Supported Container Technologies

- **Podman:** Full support.
- **Toolbox:** Supported.
- **Distrobox:** Supported.
- **JHBuild:** Supported as a special environment. For session persistence,
  add to `~/.bashrc` (or equivalent):

  ```bash
  if [ -t 1 -a x$UNDER_JHBUILD != x ]; then
      printf "\033]777;container;push;%s;jhbuild\033\\" "JHBuild"
      function pop_container {
          printf "\033]777;container;pop;;\033\\"
      }
      trap pop_container EXIT
  fi
  ```

- **Docker:** Direct, dedicated support for the Docker daemon is **not
  currently implemented**. Users can run `docker` CLI commands normally.
- **systemd-nspawn:** Contributions for enhanced integration are welcome.

### Note on Container Security

Ptyxis assumes containers are not used for strict security isolation, as most
general-purpose runtimes may not provide sufficient hardening for untrusted
workloads by default. Users should be aware of their container solutions'
security posture.

---

## Customization & Theming

- **Color Palettes:**
  - Built-in schemes, auto-switching with system light/dark mode.
  - Install custom `.palette` files in:
    - Native: `~/.local/share/org.gnome.Ptyxis/palettes/`
    - Flatpak (Stable): `~/.var/app/app.devsuite.Ptyxis/data/ptyxis/palettes/`
    - Flatpak (Nightly): `~/.var/app/org.gnome.Ptyxis.Devel/data/ptyxis/palettes/`
    - In Ptyxis 49, you can use the `--install-palette FILENAME` option to
      install a palette to the correct location.
  - "Window Dressing" applies palette colors to the window theme.
- **Keyboard Shortcuts:**
  - Most actions have configurable shortcuts via Preferences.
- **Transparency:**
  - Optional background transparency, adjustable via GSettings.
    Example (set 15% transparency, schema ID may vary):

    ```bash
    # If using distro packaging
    gsettings set org.gnome.Ptyxis.Profile:/org/gnome/Ptyxis/Profiles/$PTYXIS_PROFILE/ opacity .85

    # If using app.devsuite.Ptyxis from Flathub
    flatpak run --command=gsettings app.devsuite.Ptyxis set org.gnome.Ptyxis.Profile:/org/gnome/Ptyxis/Profiles/$PTYXIS_PROFILE/ opacity .85

    # If using org.gnome.Ptyxis.Devel Nightly Flatpak
    flatpak run --command=gsettings org.gnome.Ptyxis.Devel set org.gnome.Ptyxis.Devel.Profile:/org/gnome/Ptyxis/Devel/Profiles/$PTYXIS_PROFILE/ opacity .85
    ```

- **User Profiles:**
  - Named profiles for default container, startup commands, appearance
    (font, palette), terminal behaviors, and compatibility modes.

---

## How It Works (Agent-UI Interaction & Fallbacks)

Ptyxis UI and `ptyxis-agent` communicate via D-Bus messages over a `socketpair()`.
The agent handles PTY creation and container interaction. It's designed for
portability (e.g., GLIBC symbol versioning for `x86_64`, targeting GLIBC 2.17)
to run on older host systems.

When Ptyxis UI is a Flatpak, it uses permissions like `org.freedesktop.Flatpak`
(for `flatpak-spawn --host`) to access the host for its agent.

**Fallback:** If host agent execution fails (e.g., incompatible GLIBC on Alpine,
non-standard linkers on NixOS), Ptyxis attempts to run `ptyxis-agent` inside
its own Flatpak sandbox. This ensures basic functionality, though some
host-interaction features might be limited.

---

## Contributing: Feature Requests and Design Changes

Ptyxis uses its GitLab issue tracker primarily for engineering defects. For new
features or significant design changes:

1. **Initiate Discussion:** Start by discussing your idea or proposal, using the
    [GNOME Design Whiteboards project](https://gitlab.gnome.org/Teams/Design/whiteboards/)
2. **Develop a Specification:** For larger features, a detailed specification
    is required. This should cover:
    - How the feature should work and not work.
    - Interaction with existing features.
    - Any necessary migration strategies.
    - UI mock-ups (if applicable).
    - Testing strategy.
    - Potential risks and security considerations.
    - Ideally, an indication of who might implement it.

Once a design is well-defined, an issue can be filed on the Ptyxis tracker
referencing the design discussion/specification.

---

## Troubleshooting Issues

- **Notifications Don't Work (e.g., command completion):**
  Ensure your shell (e.g., `~/.bashrc`, `~/.zshrc`) sources
  `/etc/profile.d/vte.sh`. This script, provided by VTE packages, sets up shell hooks for VTE's terminal property
  escape sequences used by Ptyxis.

  - Verify `vte.sh` exists and is sourced by your shell.
  - If needed, manually add to your shell's rc file:
    `if [ -f /etc/profile.d/vte.sh ]; then . /etc/profile.d/vte.sh; fi`

- **GPU/Font Rendering Issues:**
  Problems with GPU rendering or font rendering (especially with HiDPI
  scaling) are often rooted in GTK, VTE, graphics drivers, or compositor
  interactions.

  - Try different fonts or rendering settings.
  - Report persistent issues to [GNOME/gtk Issues](https://gitlab.gnome.org/GNOME/gtk/-/issues)
    or [GNOME/vte Issues](https://gitlab.gnome.org/GNOME/vte/-/issues),
    providing Ptyxis/GTK/VTE versions, OS, GPU/driver, scaling factor,
    and steps to reproduce.

- **Container Detection for `toolbox`, `podman`, `distrobox`:**
  Reliable automatic container detection relies on these tools emitting VTE
  escape sequences (like OSC 777).
  - Use **recent versions** of container tools.
  - Check tool/distribution documentation for VTE integration settings.
  - If detection fails, create a Ptyxis profile to launch commands
    explicitly in the target container.

---

## License

Ptyxis is licensed under the **GNU General Public License v3.0 or later**.
The source code includes a copy of the license in the `COPYING` file.

---

## Acknowledgments

Ptyxis is primarily developed by **Christian Hergert**. It evolves concepts
from [GNOME Builder](https://apps.gnome.org/app/org.gnome.Builder/) and was
motivated by VTE performance/feature enhancements for Wayland. The aim is a
fast, feature-rich, container-aware terminal for GNOME.
