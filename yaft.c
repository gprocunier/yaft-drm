/* See LICENSE for licence details. */
/* yaft.c: include main function */
#include "yaft.h"
#include "conf.h"
#include "util.h"
#include "fb/common.h"
#include "terminal.h"
#include "ctrlseq/esc.h"
#include "ctrlseq/csi.h"
#include "ctrlseq/osc.h"
#include "ctrlseq/dcs.h"
#include "parse.h"

#if defined(USE_DRM)
#include <errno.h>
#include <fcntl.h>
#include <linux/vt.h>

int drm_req_width = 0, drm_req_height = 0;
int drm_cursor_blink = 1;
int drm_mouse_reporting = 0;
static const char *drm_exec_cmd = NULL;
static int drm_mouse_mode = 0; /* 0=auto, 1=evdev, 2=relative */

static const uint16_t arrow_cursor[16] = {
	0xC000, 0xE000, 0xF000, 0xF800,
	0xFC00, 0xFE00, 0xFF00, 0xFF80,
	0xFFC0, 0xFE00, 0xEF00, 0xCF00,
	0x0780, 0x0780, 0x03C0, 0x0000,
};

static void drm_parse_config(void)
{
	char path[256];
	const char *home = getenv("HOME");
	if (!home) return;
	snprintf(path, sizeof(path), "%s/.yaft-drm.conf", home);
	FILE *f = fopen(path, "r");
	if (!f) return;
	char line[256];
	static char cmd_buf[1024];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n') continue;
		/* strip trailing newline */
		char *nl = strchr(line, '\n');
		if (nl) *nl = '\0';

		int w, h;
		if (sscanf(line, "resolution=%dx%d", &w, &h) == 2) {
			drm_req_width = w;
			drm_req_height = h;
		} else if (strncmp(line, "command=", 8) == 0 && strlen(line) > 8) {
			strncpy(cmd_buf, line + 8, sizeof(cmd_buf) - 1);
			cmd_buf[sizeof(cmd_buf) - 1] = '\0';
			drm_exec_cmd = cmd_buf;
		} else if (strncmp(line, "mouse=", 6) == 0) {
			if (strcmp(line + 6, "evdev") == 0) drm_mouse_mode = 1;
			else if (strcmp(line + 6, "relative") == 0) drm_mouse_mode = 2;
			else if (strcmp(line + 6, "auto") == 0) drm_mouse_mode = 0;
		}
	}
	fclose(f);
}

static void drm_parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
			int w, h;
			if (strcmp(argv[i + 1], "list") == 0) {
				drm_req_width = drm_req_height = -1;
			} else if (sscanf(argv[i + 1], "%dx%d", &w, &h) == 2) {
				drm_req_width = w;
				drm_req_height = h;
			} else {
				fprintf(stderr, "invalid resolution '%s', use --res WIDTHxHEIGHT or --res list\n", argv[i + 1]);
				exit(1);
			}
			i++;
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			drm_exec_cmd = argv[i + 1];
			i++;
		} else if (strcmp(argv[i], "--mouse") == 0 && i + 1 < argc) {
			if (strcmp(argv[i + 1], "evdev") == 0) drm_mouse_mode = 1;
			else if (strcmp(argv[i + 1], "relative") == 0) drm_mouse_mode = 2;
			else if (strcmp(argv[i + 1], "auto") == 0) drm_mouse_mode = 0;
			else { fprintf(stderr, "invalid mouse mode '%s', use: auto, evdev, relative\n", argv[i + 1]); exit(1); }
			i++;
		}
	}
}

static int drm_mouse_fd = -1;
static int drm_mouse_x = 0, drm_mouse_y = 0;
static int drm_mouse_cols = 80, drm_mouse_lines = 24;
static int drm_mouse_px_w = 0, drm_mouse_px_h = 0;
static int drm_mouse_abs = 0; /* 1 = evdev absolute, 0 = PS/2 relative */
static int drm_mouse_abs_max_x = 32767, drm_mouse_abs_max_y = 32767;

#include <linux/input.h>
#include <dirent.h>

static int drm_find_evdev_abs(void)
{
	char path[64];
	char name[256];
	const char *bmc_names[] = { "Avocent", "IPMI", "iLO", "AMI", "ATEN", NULL };

	for (int n = 0; n < 32; n++) {
		snprintf(path, sizeof(path), "/dev/input/event%d", n);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0) continue;
		unsigned long evbits = 0, absbits = 0;
		ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits);
		if (evbits & (1 << EV_ABS)) {
			ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), &absbits);
			if ((absbits & (1 << ABS_X)) && (absbits & (1 << ABS_Y))) {
				memset(name, 0, sizeof(name));
				ioctl(fd, EVIOCGNAME(sizeof(name)), name);
				for (const char **d = bmc_names; *d; d++) {
					if (strstr(name, *d)) {
						fprintf(stderr, "MOUSE: evdev absolute event%d (%s)\n", n, name);
						return fd;
					}
				}
			}
		}
		close(fd);
	}
	return -1;
}

static void drm_mouse_init(int width, int height, int cols, int lines)
{
	drm_mouse_cols = cols;
	drm_mouse_lines = lines;
	drm_mouse_px_w = width;
	drm_mouse_px_h = height;
	drm_mouse_x = width / 2;
	drm_mouse_y = height / 2;

	if (drm_mouse_mode == 1 || (drm_mouse_mode == 0)) {
		/* try evdev absolute for BMC devices (auto or forced evdev) */
		drm_mouse_fd = drm_find_evdev_abs();
		if (drm_mouse_fd >= 0) {
			drm_mouse_abs = 1;
			struct input_absinfo absinfo;
			if (ioctl(drm_mouse_fd, EVIOCGABS(ABS_X), &absinfo) == 0)
				drm_mouse_abs_max_x = absinfo.maximum;
			if (ioctl(drm_mouse_fd, EVIOCGABS(ABS_Y), &absinfo) == 0)
				drm_mouse_abs_max_y = absinfo.maximum;
			if (VERBOSE)
				fprintf(stderr, "MOUSE: evdev absolute fd=%d abs_max=%dx%d\n",
					drm_mouse_fd, drm_mouse_abs_max_x, drm_mouse_abs_max_y);
			return;
		}
		if (drm_mouse_mode == 1) {
			fprintf(stderr, "MOUSE: no evdev absolute device found\n");
			return;
		}
	}

	/* PS/2 /dev/input/mice */
	drm_mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
	if (drm_mouse_fd >= 0) {
		drm_mouse_abs = 0;
		if (VERBOSE)
			fprintf(stderr, "MOUSE: PS/2 relative fd=%d\n", drm_mouse_fd);
	} else {
		if (VERBOSE)
			fprintf(stderr, "MOUSE: no mouse device found\n");
	}
}

static void drm_mouse_die(void)
{
	if (drm_mouse_fd >= 0) {
		close(drm_mouse_fd);
		drm_mouse_fd = -1;
	}
}

static int cursor_vis_x = -1, cursor_vis_y = -1;

static void drm_draw_cursor(struct framebuffer_t *fb, int px, int py, uint32_t color)
{
	if (px < 0 || py < 0) return;
	int bpp = fb->info.bytes_per_pixel;

	for (int h = 0; h < 16 && (py + h) < fb->info.height; h++) {
		uint16_t row = arrow_cursor[h];
		for (int w = 0; w < 16 && (px + w) < fb->info.width; w++) {
			if (row & (0x8000 >> w)) {
				int off = (py + h) * fb->info.line_length + (px + w) * bpp;
				if (off >= 0 && off + bpp <= fb->info.screen_size) {
					uint32_t existing;
					memcpy(&existing, fb->fp + off, bpp);
					uint32_t xored = existing ^ color;
					memcpy(fb->fp + off, &xored, bpp);
				}
			}
		}
	}
}

static int cursor_drawn_x = -1, cursor_drawn_y = -1;

static void drm_erase_cursor(struct framebuffer_t *fb)
{
	if (cursor_drawn_x >= 0) {
		drm_draw_cursor(fb, cursor_drawn_x, cursor_drawn_y, 0x00FF00FF);
		cursor_drawn_x = cursor_drawn_y = -1;
	}
}

static void drm_overlay_cursor(struct framebuffer_t *fb)
{
	if (drm_mouse_fd < 0) return;
	drm_draw_cursor(fb, drm_mouse_x, drm_mouse_y, 0x00FF00FF);
	cursor_drawn_x = drm_mouse_x;
	cursor_drawn_y = drm_mouse_y;
}

static void drm_mouse_emit(int master_fd, int buttons)
{
	static int prev_buttons = 0;
	int cell_x = (drm_mouse_x / CELL_WIDTH) + 1;
	int cell_y = (drm_mouse_y / CELL_HEIGHT) + 1;

	if (drm_mouse_reporting && buttons != prev_buttons) {
		char buf[64];
		int len;
		if ((buttons & 1) && !(prev_buttons & 1)) {
			len = snprintf(buf, sizeof(buf), "\033[<0;%d;%dM", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		} else if (!(buttons & 1) && (prev_buttons & 1)) {
			len = snprintf(buf, sizeof(buf), "\033[<0;%d;%dm", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		}
		if ((buttons & 2) && !(prev_buttons & 2)) {
			len = snprintf(buf, sizeof(buf), "\033[<2;%d;%dM", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		} else if (!(buttons & 2) && (prev_buttons & 2)) {
			len = snprintf(buf, sizeof(buf), "\033[<2;%d;%dm", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		}
		if ((buttons & 4) && !(prev_buttons & 4)) {
			len = snprintf(buf, sizeof(buf), "\033[<1;%d;%dM", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		} else if (!(buttons & 4) && (prev_buttons & 4)) {
			len = snprintf(buf, sizeof(buf), "\033[<1;%d;%dm", cell_x, cell_y);
			if (len > 0) write(master_fd, buf, len);
		}
		prev_buttons = buttons;
	}
}

static void drm_mouse_handle(int master_fd, struct framebuffer_t *fb)
{
	(void)fb;

	if (drm_mouse_abs) {
		/* evdev absolute mode */
		struct input_event ev;
		static int abs_buttons = 0;
		int btn_changed = 0;
		while (read(drm_mouse_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type == EV_ABS) {
				if (ev.code == ABS_X)
					drm_mouse_x = (int)((long)ev.value * drm_mouse_px_w / drm_mouse_abs_max_x);
				else if (ev.code == ABS_Y)
					drm_mouse_y = (int)((long)ev.value * drm_mouse_px_h / drm_mouse_abs_max_y);
			} else if (ev.type == EV_KEY) {
				if (ev.code == BTN_LEFT) { if (ev.value) abs_buttons |= 1; else abs_buttons &= ~1; btn_changed = 1; }
				else if (ev.code == BTN_RIGHT) { if (ev.value) abs_buttons |= 2; else abs_buttons &= ~2; btn_changed = 1; }
				else if (ev.code == BTN_MIDDLE) { if (ev.value) abs_buttons |= 4; else abs_buttons &= ~4; btn_changed = 1; }
			} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
				if (drm_mouse_x < 0) drm_mouse_x = 0;
				if (drm_mouse_y < 0) drm_mouse_y = 0;
				if (drm_mouse_x >= drm_mouse_px_w) drm_mouse_x = drm_mouse_px_w - 1;
				if (drm_mouse_y >= drm_mouse_px_h) drm_mouse_y = drm_mouse_px_h - 1;
				if (btn_changed) {
					drm_mouse_emit(master_fd, abs_buttons);
					btn_changed = 0;
				}
			}
		}
	} else if (drm_mouse_abs == 2) {
		/* evdev relative mode */
		struct input_event ev;
		static int rel_buttons = 0;
		int btn_changed = 0;
		while (read(drm_mouse_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			if (ev.type == EV_REL) {
				if (ev.code == REL_X) drm_mouse_x += ev.value * 3;
				else if (ev.code == REL_Y) drm_mouse_y += ev.value * 3;
			} else if (ev.type == EV_KEY) {
				if (ev.code == BTN_LEFT) { if (ev.value) rel_buttons |= 1; else rel_buttons &= ~1; btn_changed = 1; }
				else if (ev.code == BTN_RIGHT) { if (ev.value) rel_buttons |= 2; else rel_buttons &= ~2; btn_changed = 1; }
				else if (ev.code == BTN_MIDDLE) { if (ev.value) rel_buttons |= 4; else rel_buttons &= ~4; btn_changed = 1; }
			} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
				if (drm_mouse_x < 0) drm_mouse_x = 0;
				if (drm_mouse_y < 0) drm_mouse_y = 0;
				if (drm_mouse_x >= drm_mouse_px_w) drm_mouse_x = drm_mouse_px_w - 1;
				if (drm_mouse_y >= drm_mouse_px_h) drm_mouse_y = drm_mouse_px_h - 1;
				if (btn_changed) {
					drm_mouse_emit(master_fd, rel_buttons);
					btn_changed = 0;
				}
			}
		}
	} else {
		/* PS/2 relative mode (last resort) */
		uint8_t pkt[3];
		while (read(drm_mouse_fd, pkt, 3) == 3) {
			int buttons = pkt[0] & 0x07;
			int dx = ((pkt[0] & 0x10) ? (int)pkt[1] - 256 : pkt[1]) * 3;
			int dy = ((pkt[0] & 0x20) ? (int)pkt[2] - 256 : pkt[2]) * 3;

			drm_mouse_x += dx;
			drm_mouse_y -= dy;
			if (drm_mouse_x < 0) drm_mouse_x = 0;
			if (drm_mouse_y < 0) drm_mouse_y = 0;
			if (drm_mouse_x >= drm_mouse_px_w) drm_mouse_x = drm_mouse_px_w - 1;
			if (drm_mouse_y >= drm_mouse_px_h) drm_mouse_y = drm_mouse_px_h - 1;

			drm_mouse_emit(master_fd, buttons);
		}
	}
}
#endif /* USE_DRM */

void sig_handler(int signo)
{
	sigset_t sigset;
	/* global */
	extern volatile sig_atomic_t vt_active;
	extern volatile sig_atomic_t child_alive;
	extern volatile sig_atomic_t need_redraw;

	logging(DEBUG, "caught signal! no:%d\n", signo);

	if (signo == SIGCHLD) {
		child_alive = false;
		wait(NULL);
	} else if (signo == SIGUSR1) {
		if (vt_active) {           /* vt deactivate */
			vt_active = false;
			ioctl(STDIN_FILENO, VT_RELDISP, 1);

			if (BACKGROUND_DRAW) { /* update passive cursor */
				need_redraw = true;
			} else {               /* sleep until next vt switching */
				sigfillset(&sigset);
				sigdelset(&sigset, SIGUSR1);
				sigsuspend(&sigset);
			}
		} else {                   /* vt activate */
			vt_active   = true;
			need_redraw = true;
			ioctl(STDIN_FILENO, VT_RELDISP, VT_ACKACQ);
		}
	}
}

void set_rawmode(int fd, struct termios *save_tm)
{
	struct termios tm;

	tm = *save_tm;
	tm.c_iflag = tm.c_oflag = 0;
	tm.c_cflag &= ~CSIZE;
	tm.c_cflag |= CS8;
	tm.c_lflag &= ~(ECHO | ISIG | ICANON);
	tm.c_cc[VMIN]  = 1; /* min data size (byte) */
	tm.c_cc[VTIME] = 0; /* time out */
	etcsetattr(fd, TCSAFLUSH, &tm);
}

bool tty_init(struct termios *termios_orig)
{
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = sig_handler;
	sigact.sa_flags   = SA_RESTART;
	esigaction(SIGCHLD, &sigact, NULL);

	if (VT_CONTROL) {
		esigaction(SIGUSR1, &sigact, NULL);

		struct vt_mode vtm;
		vtm.mode   = VT_PROCESS;
		vtm.waitv  = 0;
		vtm.relsig = vtm.acqsig = vtm.frsig = SIGUSR1;

		if (ioctl(STDIN_FILENO, VT_SETMODE, &vtm))
			logging(WARN, "ioctl: VT_SETMODE failed (maybe here is not console)\n");

		if (FORCE_TEXT_MODE == false) {
			if (ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS))
				logging(WARN, "ioctl: KDSETMODE failed (maybe here is not console)\n");
		}
	}

	etcgetattr(STDIN_FILENO, termios_orig);
	set_rawmode(STDIN_FILENO, termios_orig);
	ewrite(STDIN_FILENO, "\033[?25l", 6); /* make cusor invisible */

	return true;
}

void tty_die(struct termios *termios_orig)
{
 	/* no error handling */
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = SIG_DFL;
	sigaction(SIGCHLD, &sigact, NULL);

	if (VT_CONTROL) {
		sigaction(SIGUSR1, &sigact, NULL);

		struct vt_mode vtm;
		vtm.mode   = VT_AUTO;
		vtm.waitv  = 0;
		vtm.relsig = vtm.acqsig = vtm.frsig = 0;

		ioctl(STDIN_FILENO, VT_SETMODE, &vtm);

		if (FORCE_TEXT_MODE == false)
			ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
	}

	tcsetattr(STDIN_FILENO, TCSAFLUSH, termios_orig);
	//fflush(stdout);
	ewrite(STDIN_FILENO, "\033[?25h", 6); /* make cursor visible */
}

bool fork_and_exec(int *master, int lines, int cols)
{
	extern const char *shell_cmd; /* defined in conf.h */
	char *shell_env;
	pid_t pid;
	struct winsize ws = {.ws_row = lines, .ws_col = cols,
		/* XXX: this variables are UNUSED (man tty_ioctl),
			but useful for calculating terminal cell size */
		.ws_ypixel = CELL_HEIGHT * lines, .ws_xpixel = CELL_WIDTH * cols};

	pid = eforkpty(master, NULL, NULL, &ws);
	if (pid < 0)
		return false;
	else if (pid == 0) { /* child */
		esetenv("TERM", term_name, 1);
#if defined(USE_DRM)
		if (drm_exec_cmd) {
			char *sh = getenv("SHELL");
			if (!sh) sh = (char *)shell_cmd;
			execl(sh, sh, "-c", drm_exec_cmd, (char *)NULL);
			perror("execl");
			exit(EXIT_FAILURE);
		}
#endif
		if ((shell_env = getenv("SHELL")) != NULL)
			eexecl(shell_env);
		else
			eexecl(shell_cmd);
		/* never reach here */
		exit(EXIT_FAILURE);
	}
	return true;
}

int check_fds(fd_set *fds, struct timeval *tv, int input, int master)
{
	int maxfd;
	FD_ZERO(fds);
	FD_SET(input, fds);
	FD_SET(master, fds);
	maxfd = (input > master) ? input : master;
#if defined(USE_DRM)
	if (drm_mouse_fd >= 0) {
		FD_SET(drm_mouse_fd, fds);
		if (drm_mouse_fd > maxfd) maxfd = drm_mouse_fd;
	}
#endif
	tv->tv_sec  = 0;
	tv->tv_usec = SELECT_TIMEOUT;
	return eselect(maxfd + 1, fds, NULL, NULL, tv);
}

int main(int argc, char **argv)
{
	uint8_t buf[BUFSIZE];
	ssize_t size;
	fd_set fds;
	struct timeval tv;
	struct framebuffer_t fb;
	struct terminal_t term;
	/* global */
	extern volatile sig_atomic_t need_redraw;
	extern volatile sig_atomic_t child_alive;
	extern struct termios termios_orig;

	/* init */
	if (setlocale(LC_ALL, "") == NULL) /* for wcwidth() */
		logging(WARN, "setlocale falied\n");

#if defined(USE_DRM)
	drm_parse_config();
	drm_parse_args(argc, argv);
#else
	(void)argc; (void)argv;
#endif

	if (!fb_init(&fb)) {
		logging(FATAL, "framebuffer initialize failed\n");
		goto fb_init_failed;
	}
	if (!term_init(&term, fb.info.width, fb.info.height)) {
		logging(FATAL, "terminal initialize failed\n");
		goto term_init_failed;
	}

	if (!tty_init(&termios_orig)) {
		logging(FATAL, "tty initialize failed\n");
		goto tty_init_failed;
	}

	/* fork and exec shell */
	if (!fork_and_exec(&term.fd, term.lines, term.cols)) {
		logging(FATAL, "forkpty failed\n");
		goto tty_init_failed;
	}
	child_alive = true;

#if defined(USE_DRM)
	drm_mouse_init(fb.info.width, fb.info.height, term.cols, term.lines);
#endif

#if defined(USE_DRM)
	int blink_counter = 0;
	int blink_visible = 1;
	int lazy_pending = 0;
#endif

	/* main loop */
	while (child_alive) {
		if (need_redraw) {
			need_redraw = false;
			cmap_update(fb.fd, fb.cmap);
			redraw(&term);
#if defined(USE_DRM)
			drm_erase_cursor(&fb);
#endif
			refresh(&fb, &term);
#if defined(USE_DRM)
			drm_overlay_cursor(&fb);
#endif
		}

		if (check_fds(&fds, &tv, STDIN_FILENO, term.fd) == -1)
			continue;

#if defined(USE_DRM)
		blink_counter++;
		if (blink_counter >= 33) {
			blink_counter = 0;
			blink_visible = !blink_visible;
			drm_cursor_blink = blink_visible;
			term.line_dirty[term.cursor.y] = true;
			drm_erase_cursor(&fb);
			refresh(&fb, &term);
			drm_overlay_cursor(&fb);
		}
#endif

		if (FD_ISSET(STDIN_FILENO, &fds)) {
			if ((size = read(STDIN_FILENO, buf, BUFSIZE)) > 0)
				ewrite(term.fd, buf, size);
		}
#if defined(USE_DRM)
		if (drm_mouse_fd >= 0 && FD_ISSET(drm_mouse_fd, &fds)) {
			drm_erase_cursor(&fb);
			drm_mouse_handle(term.fd, &fb);
			drm_overlay_cursor(&fb);
			{
				drmModeClip clip = { 0, 0, fb.info.width, fb.info.height };
				drmModeDirtyFB(fb.fd, drm_state.fb_id, &clip, 1);
			}
		}
#endif
		if (FD_ISSET(term.fd, &fds)) {
			if ((size = read(term.fd, buf, BUFSIZE)) > 0) {
				if (VERBOSE)
					ewrite(STDOUT_FILENO, buf, size);
#if defined(USE_DRM)
				/* detect xterm mouse reporting enable/disable
				   watch for ?1000h ?1002h ?1003h ?1006h and their 'l' counterparts */
				for (ssize_t s = 0; s + 7 < size; s++) {
					if (buf[s] == '\033' && buf[s+1] == '[' && buf[s+2] == '?'
					    && buf[s+3] == '1' && buf[s+4] == '0' && buf[s+5] == '0') {
						if ((buf[s+6] == '0' || buf[s+6] == '2' || buf[s+6] == '3' || buf[s+6] == '6') && buf[s+7] == 'h')
							drm_mouse_reporting = 1;
						else if ((buf[s+6] == '0' || buf[s+6] == '2' || buf[s+6] == '3') && buf[s+7] == 'l')
							drm_mouse_reporting = 0;
					}
				}
#endif
				parse(&term, buf, size);
				if (LAZY_DRAW && size == BUFSIZE) {
#if defined(USE_DRM)
					lazy_pending = 1;
#endif
					continue;
				}
#if defined(USE_DRM)
				drm_erase_cursor(&fb);
#endif
				refresh(&fb, &term);
#if defined(USE_DRM)
				drm_overlay_cursor(&fb);
#endif
			}
		}
	}

	/* normal exit */
#if defined(USE_DRM)
	drm_mouse_die();
#endif
	fb_die(&fb);
	tty_die(&termios_orig);
	term_die(&term);
	return EXIT_SUCCESS;

	/* error exit */
tty_init_failed:
	term_die(&term);
term_init_failed:
	fb_die(&fb);
fb_init_failed:
	return EXIT_FAILURE;
}
