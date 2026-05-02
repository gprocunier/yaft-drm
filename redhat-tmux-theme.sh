#!/usr/bin/env bash
# =============================================================================
# Theme: Red Hat - Dark Variant
# Description: Based on Red Hat Design System (ux.redhat.com) color tokens
# =============================================================================

declare -gA THEME_COLORS=(
    # =========================================================================
    # CORE
    # =========================================================================
    [background]="#151515"               # gray-95: Primary surface (dark scheme)

    # =========================================================================
    # STATUS BAR
    # =========================================================================
    [statusbar-bg]="#1F1F1F"             # gray-90: Secondary surface (dark scheme)
    [statusbar-fg]="#C7C7C7"             # gray-30: Text on dark surface

    # =========================================================================
    # SESSION (status-left)
    # =========================================================================
    [session-bg]="#EE0000"               # red-50: Brand red
    [session-fg]="#FFFFFF"               # white: Text on brand red
    [session-prefix-bg]="#F5921B"        # orange-40: Prefix mode
    [session-copy-bg]="#0066CC"          # blue-50: Copy mode
    [session-search-bg]="#63993D"        # green-50: Search mode
    [session-command-bg]="#5E40BE"       # purple-50: Command mode

    # =========================================================================
    # WINDOW (active)
    # =========================================================================
    [window-active-base]="#0066CC"       # blue-50: Active window (contrast with red session)
    [window-active-style]="bold"

    # =========================================================================
    # WINDOW (inactive)
    # =========================================================================
    [window-inactive-base]="#4D4D4D"     # gray-60: Inactive window
    [window-inactive-style]="none"

    # =========================================================================
    # WINDOW STATE
    # =========================================================================
    [window-activity-style]="italics"
    [window-bell-style]="bold"
    [window-zoomed-bg]="#37A3A3"         # teal-50: Zoomed indicator

    # =========================================================================
    # PANE
    # =========================================================================
    [pane-border-active]="#0066CC"       # blue-50: Active pane (matches active window)
    [pane-border-inactive]="#383838"     # gray-70

    # =========================================================================
    # STATUS COLORS
    # =========================================================================
    [ok-base]="#0066CC"                  # gray-70
    [good-base]="#63993D"               # green-50
    [info-base]="#0066CC"               # blue-50
    [warning-base]="#F5921B"            # orange-40
    [error-base]="#F0561D"              # red-orange-50: Danger/error
    [disabled-base]="#4D4D4D"           # gray-60

    # =========================================================================
    # MESSAGE COLORS
    # =========================================================================
    [message-bg]="#1F1F1F"               # gray-90
    [message-fg]="#E0E0E0"               # gray-20

    # =========================================================================
    # POPUP & MENU
    # =========================================================================
    [popup-bg]="#1F1F1F"                 # gray-90
    [popup-fg]="#E0E0E0"                 # gray-20
    [popup-border]="#A60000"             # red-60
    [menu-bg]="#1F1F1F"                  # gray-90
    [menu-fg]="#E0E0E0"                  # gray-20
    [menu-selected-bg]="#EE0000"         # red-50
    [menu-selected-fg]="#FFFFFF"         # white
    [menu-border]="#A60000"              # red-60
)
