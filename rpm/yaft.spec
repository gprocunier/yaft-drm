Name:           yaft
Version:        0.2.9
Release:        9%{?dist}
Summary:        Yet another framebuffer terminal with Sixel graphics support
License:        MIT
URL:            https://github.com/uobikiemukot/yaft
Source0:        %{url}/archive/v%{version}/%{name}-%{version}.tar.gz
Source1:        yaft-meslo-nerd-glyph.h
Source2:        yaft-terminus-nerd-glyph.h

# DRM backend, mouse, true color, DA suppression, crash handlers, -c cmd
Patch0:         yaft-drm-backend.patch

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  ncurses
BuildRequires:  pkgconfig(libdrm)

%description
yaft is a simple terminal emulator for the Linux framebuffer console.
It supports UTF-8, 256 colors, Sixel graphics, and DRCS (dynamically
redefinable character sets). No X11 or Wayland required.

The yaft-drm variants use the DRM/KMS API instead of the legacy /dev/fb0
device, with built-in mouse support via /dev/input/mice, arrow cursor,
true color approximation, and configurable resolution. Works on modern
kernels (Fedora 43+, RHEL 10+) where CONFIG_FB_DEVICE is disabled.

Two font variants are provided:
  yaft-drm           - Terminus with powerline glyphs (8x16, default)
  yaft-drm-meslo     - MesloLGL Nerd Font Mono (10x23)

%prep
%setup -q -n %{name}-%{version}
patch -p1 < %{PATCH0}

%build
export CFLAGS="%{optflags}"
# Build legacy fbdev version with default font
%make_build yaft

# Build DRM Terminus variant (default)
cp %{SOURCE2} glyph.h
cc -o yaft-drm yaft.c %{optflags} -DUSE_DRM $(pkg-config --cflags libdrm) $(pkg-config --libs libdrm)

# Build DRM Meslo variant
cp %{SOURCE1} glyph.h
cc -o yaft-drm-meslo yaft.c %{optflags} -DUSE_DRM $(pkg-config --cflags libdrm) $(pkg-config --libs libdrm)

%install
%make_install PREFIX=%{buildroot}%{_prefix} MANPREFIX=%{buildroot}%{_mandir}
install -m755 yaft-drm %{buildroot}%{_bindir}/yaft-drm
install -m755 yaft-drm-meslo %{buildroot}%{_bindir}/yaft-drm-meslo

%files
%license LICENSE
%doc README.md ChangeLog
%{_bindir}/yaft
%{_bindir}/yaft-drm
%{_bindir}/yaft-drm-meslo
%{_bindir}/yaft_wall
%{_mandir}/man1/yaft.1*
%{_datadir}/terminfo/y/yaft*

%changelog
* Tue Apr 29 2026 Greg Procunier - 0.2.9-9
- Fix mouse tracking: full-screen DRM flush (partial flush broken on QXL)
- BMC-only evdev absolute detection (Avocent, IPMI, iLO, ATEN, AMI)
- PS/2 relative fallback for VMs and physical mice
- --mouse evdev|relative|auto argument and mouse= config option
- --res list to show available modes
- Nerd Font icons from Meslo (3469 glyphs) with Terminus ASCII

* Tue Apr 29 2026 Greg Procunier - 0.2.9-8
- --res list to show supported modes
- --res with invalid input gives clear error message

* Tue Apr 29 2026 Greg Procunier - 0.2.9-7
- Performance: LAZY_DRAW re-enabled with deferred flush on idle timeout
- Performance: SELECT_TIMEOUT increased to 50ms for DRM builds
- Performance: cursor blink only fires on idle (no fd activity)
- Idle CPU 0-2%, active rendering 15-30% on Matrox G200eR2

* Tue Apr 29 2026 Greg Procunier - 0.2.9-6
- Symbols Nerd Font icons from Meslo with proper 10-to-8px scaling
- Hand-crafted powerline rounded caps (E0B4, E0B6)
- Fix mouse mode detection for SGR 1006 sequences

* Tue Apr 29 2026 Greg Procunier - 0.2.9-5
- evdev absolute mouse support for iDRAC, iLO, IPMI, and VM virtual mice
- Auto-detect evdev absolute vs PS/2 relative mouse devices
- Partial DRM dirty flush for cursor movement (performance)
- Proper button state tracking for evdev devices

* Tue Apr 29 2026 Greg Procunier - 0.2.9-4
- Terminus font as default yaft-drm (8x16, native BDF with powerline glyphs)
- MesloLGL Nerd Font Mono as yaft-drm-meslo alternate (10x23)
- Add -c command argument to run a command instead of shell
- Add command= config option in ~/.yaft-drm.conf
- Invalid --res dumps supported modes and exits cleanly
- True color (24-bit RGB) to 256-color approximation in SGR handler
- Suppress DA response to prevent tmux escape sequence leaks

* Tue Apr 29 2026 Greg Procunier - 0.2.9-3
- DRM/KMS backend with mouse, true color, Nerd Font, crash recovery

* Tue Apr 29 2026 Greg Procunier - 0.2.9-2
- Add DRM/KMS backend (yaft-drm) for kernels without CONFIG_FB_DEVICE

* Tue Apr 28 2026 Greg Procunier - 0.2.9-1
- Initial RPM package
