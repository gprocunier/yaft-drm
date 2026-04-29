# yaft-drm

![yaft-drm demo](yaft-drm-final.gif)

A DRM/KMS fork of [yaft](https://github.com/uobikiemukot/yaft) (yet another framebuffer terminal) that provides high-fidelity console terminal services on modern Linux kernels where the legacy `/dev/fb0` framebuffer device has been removed (`CONFIG_FB_DEVICE=n`).

## What This Is

Modern Linux distributions (Fedora 43+, RHEL 10+) have disabled the legacy framebuffer character device in favor of DRM/KMS. This breaks all framebuffer terminal emulators that depend on `/dev/fb0`, including the original yaft.

**yaft-drm** replaces the fbdev backend with DRM dumb buffers, enabling a full-featured terminal emulator on the bare Linux console with:

- **Sixel graphics** - render images and graphical applications like [brow6el](https://codeberg.org/janantos/brow6el) directly on the console
- **Mouse support** - built-in `/dev/input/mice` reader with arrow cursor overlay, xterm SGR mouse reporting for applications that request it
- **Powerline/Nerd Font glyphs** - Terminus font with powerline arrows baked in, plus MesloLGL Nerd Font Mono as an alternate
- **True color approximation** - 24-bit RGB escape sequences mapped to the nearest 256-color palette entry
- **Configurable resolution** - `--res WxH` argument or `~/.yaft-drm.conf`
- **Clean tmux integration** - works with tmux, Tokyo Night, and other themes with proper powerline rendering
- **Crash recovery** - signal handlers restore the console on abnormal exit, clean CRTC restore on normal exit

## Use Cases

### Terminal Web Browser on the Console

Run [brow6el](https://codeberg.org/janantos/brow6el) - a Chromium-based terminal web browser with Sixel graphics - directly on a server console with mouse interaction. No X11 or Wayland required.

```bash
sudo yaft-drm --res 1920x1080 -c "brow6el https://example.com"
```

### Headless Server Administration

Full terminal with mouse support, powerline-enabled tmux status bars, and graphical capabilities on headless servers accessed via iDRAC, iLO, IPMI, or KVM console.

```bash
sudo yaft-drm --res 1440x900
tmux new
```

### Kiosk / Emergency Console

Configure a default command in `~/.yaft-drm.conf` for a dedicated console application that launches automatically.

## Installation

### Fedora 43 / RHEL 10+ (COPR)

Pre-built RPMs are available from the [greg-at-redhat/brow6el](https://copr.fedorainfracloud.org/coprs/greg-at-redhat/brow6el/) COPR repository:

```bash
sudo dnf copr enable greg-at-redhat/brow6el
sudo dnf install yaft
```

This installs:
- `yaft-drm` - Terminus font, 8x16 cells (default)
- `yaft-drm-meslo` - MesloLGL Nerd Font Mono, 10x23 cells
- `yaft` - legacy fbdev version (for kernels with `/dev/fb0`)

The COPR also provides `brow6el` and `gpm-mouse-shim` packages.

### Build from Source

```bash
# Dependencies
sudo dnf install gcc make ncurses libdrm-devel

# Build
make yaft-drm-terminus    # Terminus font (default)
make yaft-drm-meslo       # MesloLGL Nerd Font Mono

# Install
sudo install -m755 yaft-drm-terminus /usr/local/bin/yaft-drm
sudo install -m755 yaft-drm-meslo /usr/local/bin/yaft-drm-meslo
```

### RPM Build

The `rpm/` directory contains the spec file and sources for building RPMs:

```bash
cp rpm/yaft.spec ~/rpmbuild/SPECS/
cp rpm/*.h rpm/*.patch ~/rpmbuild/SOURCES/
spectool -g -R ~/rpmbuild/SPECS/yaft.spec
rpmbuild -ba ~/rpmbuild/SPECS/yaft.spec
```

## Usage

```bash
# Basic - opens a shell on the DRM framebuffer
sudo yaft-drm

# Set resolution
sudo yaft-drm --res 1920x1080

# Run a specific command
sudo yaft-drm --res 1920x1080 -c "brow6el https://example.com"

# Use MesloLGL Nerd Font (larger cells)
sudo yaft-drm-meslo --res 1920x1080

# List supported resolutions
sudo yaft-drm --res 9999x9999
```

## Configuration

Create `~/.yaft-drm.conf`:

```ini
# Display resolution (must match a supported mode)
resolution=1920x1080

# Default command (instead of shell)
command=tmux new
```

Command-line arguments override config file settings.

## Font Variants

| Binary | Font | Cell Size | At 1920x1080 |
|---|---|---|---|
| `yaft-drm` | Terminus + powerline | 8x16 | 240x67 cells |
| `yaft-drm-meslo` | MesloLGL Nerd Font Mono | 10x23 | 192x46 cells |

## Key Differences from Upstream yaft

- DRM/KMS backend (`/dev/dri/cardN`) instead of fbdev (`/dev/fb0`)
- Auto-detection of connected DRM display
- Built-in mouse via `/dev/input/mice` with arrow cursor
- Mouse reporting gated on application enable (no escape code spam in shell)
- True color (SGR 38;2;R;G;B) to 256-color approximation
- DA response suppressed to prevent tmux escape leaks
- Crash signal handlers restore console state
- Clean exit with DRM CRTC restore and VT switch recovery
- `-c` command argument and `~/.yaft-drm.conf` config file
- `--res WxH` resolution selection with mode validation
- Baked-in Nerd Font / powerline glyph support

## Tested Hardware

| Platform | Display Adapter | Mouse Input | Status |
|---|---|---|---|
| Dell PowerEdge (iDRAC 8) | Matrox G200eR2 | Avocent USB (evdev absolute) | Validated |
| KVM/QEMU (virt-manager) | QXL / Virtio VGA | QEMU USB Tablet (evdev absolute) | Validated |
| KVM/QEMU (libvirt) | Virtio GPU | VirtualPS/2 VMware VMMouse | Validated |

### Performance (Dell iDRAC 8, Matrox G200eR2, 1440x900)

| State | CPU Usage |
|---|---|
| Idle (shell prompt) | 0-2% |
| Active terminal use | ~9% |
| brow6el web browsing | 15-30% |

## Requirements

- Linux kernel with DRM/KMS (any modern kernel)
- Root access (for `/dev/dri/` and `/dev/input/mice`)
- A display connected to a DRM output (physical, virtual, or remote console)

## Upstream

This is a fork of [uobikiemukot/yaft](https://github.com/uobikiemukot/yaft) v0.2.9.

## License

MIT (same as upstream yaft)
