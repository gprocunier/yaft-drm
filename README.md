# yaft-drm

![yaft-drm demo](yaft-drm-final.gif)

A DRM/KMS fork of [yaft](https://github.com/uobikiemukot/yaft) (yet another framebuffer terminal) that provides high-fidelity console terminal services on modern Linux kernels where the legacy `/dev/fb0` framebuffer device has been removed (`CONFIG_FB_DEVICE=n`).

## What This Is

Modern Linux distributions (Fedora 43+, RHEL 10+) have disabled the legacy framebuffer character device in favor of DRM/KMS. This breaks all framebuffer terminal emulators that depend on `/dev/fb0`, including the original yaft.

**yaft-drm** replaces the fbdev backend with DRM dumb buffers, enabling a full-featured terminal emulator on the bare Linux console with:

- **Sixel graphics** — render images and graphical applications like [brow6el](https://codeberg.org/janantos/brow6el) directly on the console
- **Mouse support** — evdev absolute for BMC/KVM hardware (iDRAC, iLO, IPMI), PS/2 relative fallback for VMs and physical mice, with arrow cursor overlay
- **Nerd Font glyphs** — Terminus ASCII with 3469 Meslo Nerd Font icons for powerline, file type icons, and status indicators
- **True color approximation** — 24-bit RGB SGR sequences mapped to the nearest 256-color palette entry
- **Configurable resolution** — `--res WxH`, `--res list`, or `~/.yaft-drm.conf`
- **Mouse mode selection** — `--mouse evdev|relative|auto` for different console environments
- **Command execution** — `-c "command"` to launch directly into an application
- **Clean tmux integration** — proper DA handling, powerline rendering, mouse click support on tmux window tabs
- **Crash recovery** — signal handlers restore console on abnormal exit, clean CRTC restore with VT switch on normal exit

## Use Cases

### Terminal Web Browser on the Console

Run [brow6el](https://codeberg.org/janantos/brow6el) — a Chromium-based terminal web browser with Sixel graphics — directly on a server console with mouse interaction. No X11 or Wayland required.

```bash
sudo yaft-drm --res 1920x1080 -c "brow6el https://example.com"
```

### Headless Server Administration

Full terminal with mouse support, powerline-enabled tmux status bars, and graphical capabilities on headless servers accessed via iDRAC, iLO, IPMI, or KVM console.

```bash
sudo yaft-drm --res 1440x900 -c "tmux new -s Console"
```

### Kiosk / Emergency Console

Configure a default command in `~/.yaft-drm.conf` for a dedicated console application.

## Installation

### Fedora 43 / RHEL 10+ (COPR)

Pre-built RPMs are available from the [greg-at-redhat/brow6el](https://copr.fedorainfracloud.org/coprs/greg-at-redhat/brow6el/) COPR repository:

```bash
sudo dnf copr enable greg-at-redhat/brow6el
sudo dnf install yaft
```

This installs:
- `yaft-drm` — Terminus + Nerd Font icons, 8x16 cells (default)
- `yaft` — legacy fbdev version (for kernels with `/dev/fb0`)

The COPR also provides `brow6el` and `gpm-mouse-shim` packages.

### Build from Source

```bash
sudo dnf install gcc make ncurses libdrm-devel

make yaft-drm-terminus    # Terminus + Nerd Font icons (default)

sudo install -m755 yaft-drm-terminus /usr/local/bin/yaft-drm
```

### RPM Build

The `rpm/` directory contains the spec file and sources:

```bash
cp rpm/yaft.spec ~/rpmbuild/SPECS/
cp rpm/*.h rpm/*.patch ~/rpmbuild/SOURCES/
spectool -g -R ~/rpmbuild/SPECS/yaft.spec
rpmbuild -ba ~/rpmbuild/SPECS/yaft.spec
```

## Usage

```bash
# Opens a shell on the DRM framebuffer
sudo yaft-drm

# Set resolution
sudo yaft-drm --res 1920x1080

# List supported resolutions
sudo yaft-drm --res list

# Run a specific command
sudo yaft-drm --res 1920x1080 -c "brow6el https://example.com"

# Force mouse mode
sudo yaft-drm --mouse evdev      # BMC absolute positioning
sudo yaft-drm --mouse relative   # PS/2 relative

# MesloLGL Nerd Font variant
```

## Configuration

`~/.yaft-drm.conf`:

```ini
# Display resolution (must match a supported mode)
resolution=1920x1080

# Default command (instead of shell)
command=tmux new -s Console

# Mouse mode: auto, evdev, relative
mouse=auto
```

Command-line arguments override config file settings.

## Tested Hardware

| Platform | Display Adapter | Mouse Input | Status |
|---|---|---|---|
| Dell PowerEdge (iDRAC 8) | Matrox G200eR2 | Avocent USB (evdev absolute) | Validated |
| KVM/QEMU (virt-manager) | QXL / Virtio VGA | VirtualPS/2 (PS/2 relative) | Validated |
| KVM/QEMU (Cockpit noVNC) | QXL | VirtualPS/2 VMware VMMouse | Validated |

### Performance (iDRAC 8, Matrox G200eR2, 1440x900)

| State | CPU Usage |
|---|---|
| Idle (shell prompt) | 0% |
| Cockpit /system page | 19-21% |
| Complex dashboards (many live gauges) | Hardware-limited |

### Performance (KVM/QEMU, QXL, 1440x900)

| State | CPU Usage |
|---|---|
| Idle (shell prompt) | 0% |

## Font Variants

| Binary | Font | Cell Size | At 1920x1080 |
|---|---|---|---|
| `yaft-drm` | Terminus + 3469 Nerd Font icons | 8x16 | 240x67 cells |

## Key Differences from Upstream yaft

- DRM/KMS backend (`/dev/dri/cardN`) instead of fbdev (`/dev/fb0`)
- Auto-detection of connected DRM display
- evdev absolute mouse for BMC hardware (iDRAC, iLO, IPMI, ATEN, AMI)
- PS/2 relative mouse fallback for VMs and physical mice
- Arrow cursor overlay with XOR visibility on any background
- Mouse reporting gated on application enable (`\033[?1000h` / `\033[?1003h`)
- True color SGR (`38;2;R;G;B`) to 256-color approximation
- DA response suppressed to prevent tmux escape leaks
- Crash signal handlers restore console state
- Clean exit with DRM CRTC restore and VT switch recovery
- `-c` command argument and `~/.yaft-drm.conf` config file
- `--res WxH` / `--res list` resolution selection with mode validation
- `--mouse evdev|relative|auto` input mode selection
- Terminus + Meslo Nerd Font icons for powerline and status bar rendering

## Requirements

- Linux kernel with DRM/KMS
- Root access (for `/dev/dri/` and `/dev/input/mice`)
- A display connected to a DRM output (physical, virtual, or remote console)

## Upstream

Fork of [uobikiemukot/yaft](https://github.com/uobikiemukot/yaft) v0.2.9.

## License

MIT (same as upstream yaft)
