# Optional VTE termprop integration for fish.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Install this file into ~/.config/fish/conf.d/ to let Ptyxis detect when
# interactive fish commands start and finish. It is intentionally not
# installed by Ptyxis.

status is-interactive
or return

set -q VTE_VERSION
or return

# The shell lifecycle termprops used below were added in VTE 0.78.
test "$VTE_VERSION" -ge 7800 2>/dev/null
or return

switch "$TERM"
    case 'xterm*' 'vte*' 'gnome*'
    case '*'
        return
end

# Avoid registering duplicate handlers if the file is sourced more than once.
set -q __ptyxis_vte_fish_integration
and return
set -g __ptyxis_vte_fish_integration 1

function __ptyxis_vte_fish_preexec --on-event fish_preexec
    printf '\e]666;vte.shell.preexec!\e\\'
end

function __ptyxis_vte_fish_postexec --on-event fish_postexec
    set -l command_status $status

    printf '\e]666;vte.shell.postexec=%d\e\\' $command_status
    printf '\e]666;vte.shell.precmd!\e\\'

    return $command_status
end
