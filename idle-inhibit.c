#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wlr/render/egl.h>
#include "xdg-shell-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"

#include <linux/input-event-codes.h>

/**
 * Usage: toplevel-decoration [mode]
 * Creates a xdg-toplevel supporting decoration negotiation. If `mode` is
 * specified, the client will prefer this decoration mode.
 */

static int width = 500, height = 300;

static struct wl_compositor *compositor = NULL;
static struct wl_seat *seat = NULL;
static struct xdg_wm_base *wm_base = NULL;
static struct zwp_idle_inhibit_manager_v1 *idle_inhibit_manager = NULL;
static struct zwp_idle_inhibitor_v1 *idle_inhibitor = NULL;

struct wlr_egl egl;
struct wl_egl_window *egl_window;
struct wlr_egl_surface *egl_surface;

static void draw(void);

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial,
		uint32_t time, uint32_t button, uint32_t state_w) {
	struct wl_surface *surface = data;

	if (button == BTN_LEFT && state_w == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (idle_inhibitor) {
			zwp_idle_inhibitor_v1_destroy(idle_inhibitor);
			idle_inhibitor = NULL;
		} else {
			idle_inhibitor =
				zwp_idle_inhibit_manager_v1_create_inhibitor(
					idle_inhibit_manager,
					surface);
		}
	}

	draw();
}

static void noop () {}

static const struct wl_pointer_listener pointer_listener = {
	.enter = noop,
	.leave = noop,
	.motion = noop,
	.button = pointer_handle_button,
	.axis = noop,
	.frame = noop,
	.axis_source = noop,
	.axis_stop = noop,
	.axis_discrete = noop,
};

static void draw(void) {
	eglMakeCurrent(egl.display, egl_surface, egl_surface, egl.context);

	float color[] = {1.0, 1.0, 0.0, 1.0};
	if (idle_inhibitor) {
		color[0] = 0.0;
	}

	glViewport(0, 0, width, height);
	glClearColor(color[0], color[1], color[2], 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(egl.display, egl_surface);
}

static void xdg_surface_handle_configure(void *data,
		struct xdg_surface *xdg_surface, uint32_t serial) {
	xdg_surface_ack_configure(xdg_surface, serial);
	wl_egl_window_resize(egl_window, width, height, 0, 0);
	draw();
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data,
		struct xdg_toplevel *xdg_toplevel, int32_t w, int32_t h,
		struct wl_array *states) {
	width = w;
	height = h;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
};

// static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
// 	.preferred_mode = decoration_handle_preferred_mode,
// 	.configure = decoration_handle_configure,
// };

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, "wl_compositor") == 0) {
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface,
			1);
	} else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
		wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	} else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
		idle_inhibit_manager = wl_registry_bind(registry, name,
			&zwp_idle_inhibit_manager_v1_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		seat = wl_registry_bind(registry, name, &wl_seat_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {
	// TODO
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(int argc, char **argv) {
//	if (argc == 2) {
//		char *mode = argv[1];
//		if (strcmp(mode, "client") == 0) {
//			decoration_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT;
//		} else if (strcmp(mode, "server") == 0) {
//			decoration_mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER;
//		} else {
//			fprintf(stderr, "Invalid decoration mode\n");
//			return EXIT_FAILURE;
//		}
//	}

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "Failed to create display\n");
		return EXIT_FAILURE;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (compositor == NULL) {
		fprintf(stderr, "wl-compositor not available\n");
		return EXIT_FAILURE;
	}
	if (wm_base == NULL) {
		fprintf(stderr, "xdg-shell not available\n");
		return EXIT_FAILURE;
	}
	if (idle_inhibit_manager == NULL) {
		fprintf(stderr, "idle-inhibit not available\n");
		return EXIT_FAILURE;
	}

	wlr_egl_init(&egl, EGL_PLATFORM_WAYLAND_EXT, display, NULL,
		WL_SHM_FORMAT_ARGB8888);

	struct wl_surface *surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface =
		xdg_wm_base_get_xdg_surface(wm_base, surface);
	struct xdg_toplevel *xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

	idle_inhibitor =
		zwp_idle_inhibit_manager_v1_create_inhibitor(idle_inhibit_manager,
		surface);

	struct wl_pointer *pointer = wl_seat_get_pointer(seat);
	wl_pointer_add_listener(pointer, &pointer_listener, surface);


	xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
	xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

	wl_surface_commit(surface);

	egl_window = wl_egl_window_create(surface, width, height);
	egl_surface = wlr_egl_create_surface(&egl, egl_window);

	wl_display_roundtrip(display);

	draw();

	while (wl_display_dispatch(display) != -1) {
		// No-op
	}

	return EXIT_SUCCESS;
}
