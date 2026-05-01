/* See LICENSE for licence details. */
/* DRM/KMS backend for modern kernels without CONFIG_FB_DEVICE */
#include <errno.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <dirent.h>

/* DRM backend does not use colormaps; provide stubs */
typedef struct { uint16_t *red, *green, *blue; int start, len; } cmap_t;

enum {
	CMAP_COLOR_LENGTH = 16,
};

void alloc_cmap(cmap_t *cmap, int colors)
{
	cmap->start  = 0;
	cmap->len    = colors;
	cmap->red    = (uint16_t *) ecalloc(colors, sizeof(uint16_t));
	cmap->green  = (uint16_t *) ecalloc(colors, sizeof(uint16_t));
	cmap->blue   = (uint16_t *) ecalloc(colors, sizeof(uint16_t));
}

int put_cmap(int fd, cmap_t *cmap)
{
	(void)fd; (void)cmap;
	return 0;
}

int get_cmap(int fd, cmap_t *cmap)
{
	(void)fd; (void)cmap;
	return 0;
}

extern int drm_req_width, drm_req_height;

struct drm_state {
	uint32_t connector_id;
	uint32_t crtc_id;
	uint32_t fb_id;
	uint32_t handle;
	uint64_t mmap_offset;
	drmModeCrtc *saved_crtc;
	drmModeModeInfo mode;
};

static struct drm_state drm_state;

static void set_bitfield(struct fb_info_t *info)
{
	info->red.length   = 8;
	info->red.offset   = 16;
	info->green.length = 8;
	info->green.offset = 8;
	info->blue.length  = 8;
	info->blue.offset  = 0;
}

/* find a DRM card with a connected display */
static int drm_open_card(void)
{
	DIR *dir;
	struct dirent *ent;
	char path[64];
	int fd;
	drmModeRes *res;

	dir = opendir("/dev/dri");
	if (!dir)
		return -1;

	while ((ent = readdir(dir)) != NULL) {
		if (strncmp(ent->d_name, "card", 4) != 0)
			continue;
		snprintf(path, sizeof(path), "/dev/dri/%s", ent->d_name);
		fd = open(path, O_RDWR);
			if (fd >= 0) fcntl(fd, F_SETFD, FD_CLOEXEC);
		if (fd < 0)
			continue;
		res = drmModeGetResources(fd);
		if (res && res->count_connectors > 0) {
			for (int i = 0; i < res->count_connectors; i++) {
				drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
				if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes > 0) {
					drmModeFreeConnector(c);
					drmModeFreeResources(res);
					closedir(dir);
					logging(DEBUG, "using DRM device: %s\n", path);
					return fd;
				}
				if (c)
					drmModeFreeConnector(c);
			}
			drmModeFreeResources(res);
		}
		close(fd);
	}
	closedir(dir);
	return -1;
}

static void drm_crash_handler(int signo)
{
	ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
	signal(signo, SIG_DFL);
	raise(signo);
}

static void drm_install_crash_handlers(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = drm_crash_handler;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}

bool set_fbinfo(int fd, struct fb_info_t *info)
{
	drmModeRes *res = NULL;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	struct drm_mode_create_dumb creq;
	struct drm_mode_map_dumb mreq;
	int i;

	res = drmModeGetResources(fd);
	if (!res) {
		logging(ERROR, "drmModeGetResources failed\n");
		return false;
	}

	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (conn && conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0)
			break;
		if (conn)
			drmModeFreeConnector(conn);
		conn = NULL;
	}

	if (!conn) {
		logging(ERROR, "no connected display found\n");
		drmModeFreeResources(res);
		return false;
	}

	drm_state.mode = conn->modes[0];
	if (drm_req_width == -1) {
		fprintf(stderr, "Supported modes:\n");
		for (i = 0; i < conn->count_modes; i++) {
			fprintf(stderr, "  %dx%d%s\n",
				conn->modes[i].hdisplay, conn->modes[i].vdisplay,
				(conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) ? " (preferred)" : "");
		}
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return false;
	} else if (drm_req_width > 0 && drm_req_height > 0) {
		int found = 0;
		for (i = 0; i < conn->count_modes; i++) {
			if ((int)conn->modes[i].hdisplay == drm_req_width &&
			    (int)conn->modes[i].vdisplay == drm_req_height) {
				drm_state.mode = conn->modes[i];
				found = 1;
				break;
			}
		}
		if (!found) {
			extern int drm_list_modes;
			drm_list_modes = 1;
			fprintf(stderr, "resolution %dx%d not available. Supported modes:\n",
				drm_req_width, drm_req_height);
			for (i = 0; i < conn->count_modes; i++) {
				fprintf(stderr, "  %dx%d%s\n",
					conn->modes[i].hdisplay, conn->modes[i].vdisplay,
					(conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) ? " (preferred)" : "");
			}
			drmModeFreeConnector(conn);
			drmModeFreeResources(res);
			return false;
		}
	} else {
		for (i = 0; i < conn->count_modes; i++) {
			if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
				drm_state.mode = conn->modes[i];
				break;
			}
		}
	}

	drm_state.connector_id = conn->connector_id;

	if (conn->encoder_id) {
		enc = drmModeGetEncoder(fd, conn->encoder_id);
	}
	if (enc) {
		drm_state.crtc_id = enc->crtc_id;
		drmModeFreeEncoder(enc);
	} else {
		for (i = 0; i < res->count_crtcs; i++) {
			drm_state.crtc_id = res->crtcs[i];
			break;
		}
	}

	drm_state.saved_crtc = drmModeGetCrtc(fd, drm_state.crtc_id);

	memset(&creq, 0, sizeof(creq));
	creq.width  = drm_state.mode.hdisplay;
	creq.height = drm_state.mode.vdisplay;
	creq.bpp    = 32;

	if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq)) {
		logging(ERROR, "DRM_IOCTL_MODE_CREATE_DUMB failed\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return false;
	}

	drm_state.handle = creq.handle;

	if (drmModeAddFB(fd, creq.width, creq.height, 24, 32,
			 creq.pitch, creq.handle, &drm_state.fb_id)) {
		logging(ERROR, "drmModeAddFB failed\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return false;
	}

	info->width        = creq.width;
	info->height       = creq.height;
	info->bits_per_pixel  = 32;
	info->bytes_per_pixel = 4;
	info->line_length  = creq.pitch;
	info->screen_size  = creq.size;
	info->type         = YAFT_FB_TYPE_PACKED_PIXELS;
	info->visual       = YAFT_FB_VISUAL_TRUECOLOR;

	set_bitfield(info);

	memset(&mreq, 0, sizeof(mreq));
	mreq.handle = creq.handle;
	if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
		logging(ERROR, "DRM_IOCTL_MODE_MAP_DUMB failed\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return false;
	}

	drm_state.mmap_offset = mreq.offset;

	/* unbind fbcon from this VT so we can take over the CRTC */
	{
		int ttyfd = open("/dev/tty0", O_RDWR);
		if (ttyfd >= 0) {
			ioctl(ttyfd, KDSETMODE, KD_GRAPHICS);
			close(ttyfd);
		}
	}

	/* claim DRM master */
	drmSetMaster(fd);

	if (drmModeSetCrtc(fd, drm_state.crtc_id, drm_state.fb_id, 0, 0,
			   &drm_state.connector_id, 1, &drm_state.mode)) {
		logging(ERROR, "drmModeSetCrtc failed (errno %d)\n", errno);
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return false;
	}

	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	drm_install_crash_handlers();

	return true;
}
