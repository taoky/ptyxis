# About This Fork

[简体中文](FORK.zh.md)

> [!WARNING]
> Most changes in this fork were designed and implemented with the assistance of
> LLM agents. Although individual changes have received targeted builds and
> tests, the fork has **not** been completely tested as a whole. Expect bugs and
> regressions, review the code before relying on it, and use it with caution.
>
> In the worst case, the fork build might crash and your work would be lost.
>
> Considering this, the modifications in this fork is highly impossible to be
> merged by upstream. You can use it whatever you like.

## Select to copy

A feature I like, but [temporarily denied by upstream](https://gitlab.gnome.org/chergert/ptyxis/-/work_items/276) as title suggests.

## Optional fish shell integration

Ptyxis relies on VTE shell integration to detect when an interactive command
starts and finishes. VTE currently installs this integration for bash and zsh,
but not for fish. Without it, fish sessions do not produce Ptyxis's
command-completed notifications.

This fork provides an optional fish integration script at
[`contrib/fish/ptyxis-vte.fish`](contrib/fish/ptyxis-vte.fish). It is not
installed by Ptyxis. To enable it for the current user, either copy it into
fish's configuration directory:

```sh
mkdir -p ~/.config/fish/conf.d
cp contrib/fish/ptyxis-vte.fish ~/.config/fish/conf.d/
```

or create a symlink while working from a persistent checkout:

```sh
mkdir -p ~/.config/fish/conf.d
ln -s "$(pwd)/contrib/fish/ptyxis-vte.fish" ~/.config/fish/conf.d/ptyxis-vte.fish
```

The script only activates in an interactive fish running under VTE 0.78 or
newer. It uses fish's `fish_preexec` and `fish_postexec` events and does not
replace or wrap the user's prompt. Containers and remote hosts need their own
copy if fish is started there.

## Background bell notifications

When a terminal bell comes from a background tab or pane, or while its Ptyxis
window is inactive, the fork sends a desktop notification in addition to
marking the tab as needing attention. Repeated bells from the same pane replace
the existing notification. Activating it returns to the originating window,
tab, and exact pane; focusing or closing that pane withdraws the notification.

A bell from the visible pane of the active window keeps the existing audible
and visual bell behavior and does not create a redundant desktop notification.

## Terminal panes and splitting

The largest change is support for multiple terminal panes inside one tab.

- A `PtyxisPane` abstraction now owns the state that previously belonged to a
  whole tab, including the terminal widget, profile, zoom, child process,
  process monitoring, OSC title policy, notifications, and launch/session
  state.
- A split-tree model represents nested horizontal and vertical layouts. Panes
  can be split automatically or in an explicitly selected direction, focused
  by direction, and closed independently.
- New split dividers start centered. Closing a pane moves focus before
  collapsing the tree and does not close the tab while another pane remains.
- Terminal settings and profile changes are applied to the correct pane rather
  than only to whichever pane happens to be active.
- Tab-level compatibility properties follow the active pane, while background
  pane process events, notifications, agent updates, and profile lifecycles are
  kept associated with their originating pane.
- Close confirmation and the running-process dialog operate on the relevant
  pane or enumerate processes from every pane as appropriate.
- Split regions are exposed to accessibility tools, and the selected pane is
  visually highlighted when navigating from search results.

The default configurable shortcuts (as Tilix) are:

| Operation | Default shortcut |
| --- | --- |
| Split **a**utomatically | `Ctrl+Alt+A` |
| Split horizontally (top/bottom, **d**own) | `Ctrl+Alt+D` |
| Split vertically (left/**r**ight) | `Ctrl+Alt+R` |
| Focus pane left/right/up/down | `Alt+Arrow` |

The terminal context menu includes all three split operations and displays
their configured shortcuts. Directional pane-focus shortcuts are configurable
in Preferences.

## Geometry and session restoration

- Session format version 2 serializes the complete nested split layout, pane
  identities, active pane, divider ratios, and the full terminal grid size.
- Restoring a session reconstructs the split tree and window geometry instead
  of flattening panes into tabs.
- Window sizing ignores the aggregate grid of a split tab. Creating a split,
  returning to a split tab, or opening a new tab from a split layout should no
  longer unexpectedly resize the window to the terminal minimum (commonly
  80x24).
- Pane identity remains stable across focus changes so delayed callbacks and
  restored state can still target the intended pane.

The new session representation is a downstream format extension. Compatibility
with other Ptyxis builds, especially when moving saved sessions back and forth,
has not been comprehensively tested.

## Search and terminal navigation

- The existing tab overview searches and displays titles from every pane in a
  split tab rather than only the active pane.
- `Ctrl+Shift+P` opens a cross-window “Go to Terminal” picker. It searches all
  panes in all Ptyxis windows in the current application process, uses
  case-insensitive multi-token fuzzy matching, and orders useful results with
  recent terminals in mind.
- Activating a result selects its window, tab, and exact pane, then transfers
  keyboard focus to the terminal.
- The picker supports keyboard activation and pointer activation. `Escape`
  closes it.
- The shortcut is configurable in Preferences. The existing
  `Ctrl+Shift+O` tab overview remains available and has not been replaced.

## Quake-style window

- `ptyxis --toggle-quake` shows or hides a dedicated persistent terminal
  window. Repeated invocations reuse that window rather than creating ordinary
  windows.
- The Quake window is excluded from normal most-recent-window selection,
  ordinary sizing decisions, and session persistence.
- Its header bar and styling identify it as a Quake terminal so it is visually
  distinguishable from a normal Ptyxis window.
- The Global Shortcuts portal provides an application-wide shortcut with
  `Ctrl+grave` (Ctrl plus the grave-accent key) as its preferred trigger.
  Existing portal bindings are restored silently; if none exists, running
  `ptyxis --toggle-quake` requests the binding and the desktop may show a
  permission dialog.
- Portal activations use the compositor-provided activation token. If the
  Quake window is hidden or visible but unfocused, the shortcut presents it in
  the foreground; if it is already focused, the shortcut hides it. Invoking
  `ptyxis --toggle-quake` without an activation token retains the original
  visible/hidden toggle behavior.

Window placement and animation remain the compositor's responsibility. In
particular, the Wayland protocol does not let a normal application reliably
choose an absolute screen-edge position, so this is not guaranteed to behave
like a compositor-native drop-down terminal on every desktop.

## Color palettes

- The separate bundled `Solarized Light` palette was removed.
- The remaining Solarized palette follows the system light/dark preference and
  uses Ghostty-inspired ANSI colors with improved contrast for terminal
  applications such as `htop`.

This changes the appearance of Solarized for existing users of the bundled
palette. Color rendering has been checked for the reported visibility issues,
but not exhaustively across terminal applications.

## Distribution packaging

This fork adds a local packaging workflow for:

- Debian sid and Ubuntu 26.04 (`.deb`)
- Fedora 44 (`.rpm`)
- Arch Linux (`.pkg.tar.zst`)
- Flatpak with the GNOME 50 runtime (`.flatpak`)

`packaging/build-packages.sh` builds native packages with Docker or Podman and
derives reproducible snapshot versions from the Git revision. It refuses to
package tracked working-tree changes and builds from `git archive HEAD`.
Flatpak scratch data is kept below `dist/` so the OSTree repository and build
directory remain on the same filesystem.

See [docs/PACKAGING.md](docs/PACKAGING.md) for target requirements, commands,
artifact locations, installation notes, and the package-level verification
checklist. Packages are unsigned and use the normal Ptyxis package name and
application ID, so installing one may replace a distribution-provided build.

## Test coverage and remaining risk

The branch includes unit tests for the split-tree model and terminal-picker
matching. Changes have also received targeted Meson builds and selected package
builds during development. These checks do not constitute full validation of
the fork.

Areas that especially need broader manual testing include:

- deeply nested split creation, closure, focus, and resize behavior;
- session migration and restoration across unusual layouts and failures;
- child-process, notification, profile, and container behavior in background
  panes;
- accessibility with multiple assistive technologies;
- the Quake window under different Wayland compositors and X11 window managers;
- keyboard focus and dialog lifecycle behavior across GTK/libadwaita versions;
- installation, upgrades, and runtime integration for every packaging target;
- color readability across applications, themes, and display configurations.

Do not treat the existence of a build or unit test as evidence that all GUI,
session, compositor, or packaging paths are production-ready.
