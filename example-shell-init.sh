#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="example-shell-init"

log() { printf '[%s] %s\n' "$SCRIPT_NAME" "$*"; }
fail() { printf '[%s] ERROR: %s\n' "$SCRIPT_NAME" "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

DO_CALABI_SHELL=false
DO_TMUX_CONFIG=false
DO_TMUX_GLOBAL_CONFIG=false
DO_GIT=false
DO_YAFT_LOGIN=false
DO_FORCE=false
DO_ALL=true

usage() {
  cat <<EOF
Usage: $0 [OPTIONS]

Set up a terminal environment with calabi-shell prompt, tmux with Tokyo Night
theme, git, and optionally yaft-drm as a login shell.

With no arguments, runs: --calabi-shell --tmux-config --git

Options:
  --calabi-shell       Install calabi-shell (starship prompt) system-wide
  --tmux-config        Install tmux and Tokyo Night theme for the current user
  --tmux-global-config Install tmux and Tokyo Night theme as system default
  --git                Install git
  --yaft-login         Configure yaft-drm as an available login shell
  --force              Bypass distribution check
  -h, --help           Show this help

Notes:
  --calabi-shell, --tmux-global-config, and --yaft-login require root.
  --yaft-login configures yaft-drm in /etc/shells and enables fallback mode
  but does NOT change any user's shell. Use chsh to switch after install.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --calabi-shell)       DO_CALABI_SHELL=true; DO_ALL=false ;;
    --tmux-config)        DO_TMUX_CONFIG=true; DO_ALL=false ;;
    --tmux-global-config) DO_TMUX_GLOBAL_CONFIG=true; DO_ALL=false ;;
    --git)                DO_GIT=true; DO_ALL=false ;;
    --yaft-login)         DO_YAFT_LOGIN=true; DO_ALL=false ;;
    --force)              DO_FORCE=true ;;
    -h|--help)            usage; exit 0 ;;
    *)
      printf '[%s] ERROR: unknown argument: %s\n' "$SCRIPT_NAME" "$arg" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if $DO_ALL; then
  DO_CALABI_SHELL=true
  DO_TMUX_CONFIG=true
  DO_GIT=true
fi

require_root() {
  [ "$(id -u)" -eq 0 ] || fail "$1 requires root"
}

install_git() {
  log "installing git"
  if have git; then
    log "git already installed at $(command -v git)"
    return 0
  fi
  have dnf || fail "dnf is required to install packages"
  dnf install -y git
  log "git installed"
}

install_calabi_shell() {
  require_root "--calabi-shell"
  have git || fail "git is required (run with --git first)"

  local tmpdir
  tmpdir="$(mktemp -d)"
  trap "rm -rf '$tmpdir'" EXIT

  log "cloning calabi-shell"
  git clone https://github.com/gprocunier/calabi-shell.git "$tmpdir/calabi-shell"

  if $DO_FORCE; then
    log "running calabi-shell install --system (--force: bypassing distro check)"
    (cd "$tmpdir/calabi-shell" && sed -i 's/check_platform_support/# check_platform_support/' install.sh && ./install.sh --system)
  else
    log "running calabi-shell install --system"
    (cd "$tmpdir/calabi-shell" && ./install.sh --system)
  fi

  rm -rf "$tmpdir"
  trap - EXIT
  log "calabi-shell installed"
}

install_redhat_theme() {
  local plugin_dir="$1"
  local theme_dir="$plugin_dir/src/themes/redhat"
  if [ -f "$theme_dir/dark.sh" ]; then
    log "Red Hat theme already installed"
    return 0
  fi
  mkdir -p "$theme_dir"

  local script_dir
  script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  if [ -f "$script_dir/redhat-tmux-theme.sh" ]; then
    cp "$script_dir/redhat-tmux-theme.sh" "$theme_dir/dark.sh"
  else
    log "redhat-tmux-theme.sh not found, fetching from upstream"
    curl -fsSL "https://raw.githubusercontent.com/gprocunier/yaft-drm/main/redhat-tmux-theme.sh" \
      -o "$theme_dir/dark.sh"
  fi
  chmod 644 "$theme_dir/dark.sh"
  log "Red Hat theme installed to $theme_dir"
}

write_tmux_user_config() {
  have tmux || {
    log "installing tmux"
    have dnf || fail "dnf is required to install packages"
    dnf install -y tmux
  }

  local plugin_dir="$HOME/.config/tmux/plugins/tmux-tokyo-night"
  if [ -d "$plugin_dir" ]; then
    log "PowerKit theme engine already present at $plugin_dir"
  else
    have git || fail "git is required (run with --git first)"
    log "installing PowerKit theme engine to $plugin_dir"
    mkdir -p "$(dirname "$plugin_dir")"
    git clone https://github.com/fabioluciano/tmux-tokyo-night.git "$plugin_dir"
  fi

  install_redhat_theme "$plugin_dir"

  local tmux_conf="$HOME/.tmux.conf"
  log "writing $tmux_conf"

  cat > "$tmux_conf" <<'TMUXCONF'
# Mouse support
set -g mouse on

# Vi keys in copy mode
set -g mode-keys vi

# Vi-style copy mode bindings
bind -T copy-mode-vi v send -X begin-selection
TMUXCONF

  if have wl-copy; then
    cat >> "$tmux_conf" <<'TMUXCONF'
# Wayland clipboard integration
set -g copy-command 'wl-copy'
bind -T copy-mode-vi y send -X copy-pipe-and-cancel 'wl-copy'
bind -T copy-mode-vi MouseDragEnd1Pane send -X copy-pipe-and-cancel 'wl-copy'
TMUXCONF
  else
    cat >> "$tmux_conf" <<'TMUXCONF'
bind -T copy-mode-vi y send -X copy-pipe-and-cancel
TMUXCONF
  fi

  cat >> "$tmux_conf" <<TMUXCONF

# Scrollback history
set -g history-limit 100000

# True color support
set -g default-terminal "tmux-256color"
set -ag terminal-overrides ",*:RGB"

# Red Hat theme (via PowerKit)
set -g @powerkit_theme 'redhat'
set -g @powerkit_theme_variant 'dark'
run $plugin_dir/tmux-powerkit.tmux

# Status bar position
set -g status-position bottom
set -g allow-passthrough on
TMUXCONF

  log "tmux user config installed"
}

write_tmux_global_config() {
  require_root "--tmux-global-config"

  have tmux || {
    log "installing tmux"
    have dnf || fail "dnf is required to install packages"
    dnf install -y tmux
  }

  local plugin_dir="/usr/share/tmux/plugins/tmux-tokyo-night"
  if [ -d "$plugin_dir" ]; then
    log "PowerKit theme engine already present at $plugin_dir"
  else
    have git || fail "git is required (run with --git first)"
    log "installing PowerKit theme engine to $plugin_dir"
    git clone https://github.com/fabioluciano/tmux-tokyo-night.git "$plugin_dir"
  fi

  install_redhat_theme "$plugin_dir"

  local tmux_conf="/etc/tmux.conf"
  log "writing $tmux_conf"

  cat > "$tmux_conf" <<TMUXCONF
# Default shell (required when yaft-drm is a user's login shell)
set -g default-shell /bin/bash

# Mouse support
set -g mouse on

# Vi keys in copy mode
set -g mode-keys vi

# Vi-style copy mode bindings
bind -T copy-mode-vi v send -X begin-selection
bind -T copy-mode-vi y send -X copy-pipe-and-cancel

# Scrollback history
set -g history-limit 100000

# True color support
set -g default-terminal "tmux-256color"
set -ag terminal-overrides ",*:RGB"

# Red Hat theme (via PowerKit)
set -g @powerkit_theme "redhat"
set -g @powerkit_theme_variant "dark"
run ${plugin_dir}/tmux-powerkit.tmux

# Status bar position
set -g status-position bottom
set -g allow-passthrough on
TMUXCONF

  log "tmux global config installed"
}

configure_yaft_login() {
  require_root "--yaft-login"

  local yaft_bin="/usr/bin/yaft-drm"
  [ -x "$yaft_bin" ] || fail "yaft-drm not found at $yaft_bin (install the yaft package first)"

  if grep -qxF "$yaft_bin" /etc/shells 2>/dev/null; then
    log "yaft-drm already in /etc/shells"
  else
    log "adding yaft-drm to /etc/shells"
    echo "$yaft_bin" >> /etc/shells
  fi

  local conf="/etc/yaft-drm.conf"
  if [ -f "$conf" ] && grep -q "fallback=true" "$conf" 2>/dev/null; then
    log "fallback=true already set in $conf"
  else
    log "writing fallback=true to $conf"
    echo "fallback=true" >> "$conf"
  fi

  if systemctl is-active --quiet gpm 2>/dev/null; then
    log "stopping gpm (conflicts with yaft-drm built-in mouse)"
    systemctl stop gpm
  fi
  if systemctl is-enabled --quiet gpm 2>/dev/null; then
    log "disabling gpm (yaft-drm provides its own mouse input)"
    systemctl disable gpm
  fi

  log "yaft-drm configured as available login shell"
  log "yaft-drm provides built-in mouse input — gpm has been disabled"
  log "to switch a user's shell: chsh <username> -s $yaft_bin"
}

main() {
  $DO_GIT && install_git
  $DO_CALABI_SHELL && install_calabi_shell
  $DO_TMUX_CONFIG && write_tmux_user_config
  $DO_TMUX_GLOBAL_CONFIG && write_tmux_global_config
  $DO_YAFT_LOGIN && configure_yaft_login

  log "done"
}

main
