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
		}
	}
	fclose(f);
}

static void drm_parse_args(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--res") == 0 && i + 1 < argc) {
			int w, h;
			if (sscanf(argv[i + 1], "%dx%d", &w, &h) == 2) {
				drm_req_width = w;
				drm_req_height = h;
			}
			i++;
		} else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
			drm_exec_cmd = argv[i + 1];
			i++;
		}
	}
}

static int drm_mouse_fd = -1;
static int drm_mouse_x = 0, drm_mouse_y = 0;
static int drm_mouse_cols = 80, drm_mouse_lines = 24;
static int drm_mouse_px_w = 0, drm_mouse_px_h = 0;

static void drm_mouse_init(int width, int height, int cols, int lines)
{
	drm_mouse_fd = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
	if (drm_mouse_fd >= 0) {
		drm_mouse_cols = cols;
		drm_mouse_lines = lines;
		drm_mouse_px_w = width;
		drm_mouse_px_h = height;
		drm_mouse_x = width / 2;
		drm_mouse_y = height / 2;
		if (VERBOSE)
			fprintf(stderr, "MOUSE: opened /dev/input/mice fd=%d res=%dx%d cells=%dx%d\n",
				drm_mouse_fd, width, height, cols, lines);
	} else {
		if (VERBOSE)
			fprintf(stderr, "MOUSE: failed to open /dev/input/mice (errno=%d)\n", errno);
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

static void drm_mouse_handle(int master_fd, struct framebuffer_t *fb)
{
	(void)fb;
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

		/* convert pixel position to cell position for xterm escape */
		int cell_x = (drm_mouse_x / CELL_WIDTH) + 1;
		int cell_y = (drm_mouse_y / CELL_HEIGHT) + 1;

		/* generate xterm SGR mouse events only when app has enabled reporting */
		static int prev_buttons = 0;
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

		if (check_fds(&fds, &tv, STDIN_FILENO, term.fd) == -1)
			continue;

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
						if ((buf[s+6] == '0' || buf[s+6] == '2' || buf[s+6] == '3') && buf[s+7] == 'h')
							drm_mouse_reporting = 1;
						else if ((buf[s+6] == '0' || buf[s+6] == '2' || buf[s+6] == '3') && buf[s+7] == 'l')
							drm_mouse_reporting = 0;
					}
				}
#endif
				parse(&term, buf, size);
				if (LAZY_DRAW && size == BUFSIZE)
					continue;
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
