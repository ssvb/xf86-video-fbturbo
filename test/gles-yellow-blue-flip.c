/* gcc -O2 -o gles-yellow-blue-flip gles-yellow-blue-flip.c -lEGL -lGLESv2 -lX11 */

/*
 * A testcase for xf86-video-mali bug. Based on the GLES triangle test
 * from https://github.com/linux-sunxi/sunxi-mali repository.
 *
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Hello triangle, adapted for native display on libMali.so.
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#define USE_X

#ifdef USE_X
#include  <X11/Xatom.h>
#include  <X11/Xlib.h>
#include  <X11/Xutil.h>
#endif

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#define WIDTH 480
#define HEIGHT 480

#ifdef USE_X
Display *XDisplay;
Window XWindow;
#else
struct mali_native_window native_window = {
	.width = WIDTH,
	.height = HEIGHT,
};
#endif

static EGLint const config_attribute_list[] = {
	EGL_RED_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_ALPHA_SIZE, 8,
	EGL_BUFFER_SIZE, 32,

	EGL_STENCIL_SIZE, 0,
	EGL_DEPTH_SIZE, 0,

	EGL_SAMPLES, 4,

	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,

	EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PIXMAP_BIT,


	EGL_NONE
};

static EGLint window_attribute_list[] = {
	EGL_NONE
};

static const EGLint context_attribute_list[] = {
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

EGLDisplay egl_display;
EGLSurface egl_surface;

static int framecount = 0;

void
Redraw(int width, int height)
{
	int i;
	glViewport(0, 0, width, height);

	framecount++;

	glClearColor(1.0, 0.0, 0.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	sleep(1);

	if (framecount & 1)
		glClearColor(1.0, 1.0, 0.0, 1.0);
	else
		glClearColor(0.0, 0.0, 1.0, 1.0);

	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(egl_display, egl_surface);
}

void set_fullscreen()
{
#ifdef USE_X
	Display *display = XDisplay;
	Window window = XWindow;
	XEvent xev;

	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.send_event = True;
	xev.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
	xev.xclient.window = window;
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 1;
	xev.xclient.data.l[1] = XInternAtom(display,
					    "_NET_WM_STATE_FULLSCREEN", False);
	xev.xclient.data.l[2] = 0;
	xev.xclient.data.l[3] = 0;
	xev.xclient.data.l[4] = 0;

	XSendEvent(display, DefaultRootWindow(display), False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
	XSync(display, False);
#endif
}


int
main(int argc, char *argv[])
{
	EGLint egl_major, egl_minor;
	EGLConfig config;
	EGLint num_config;
	EGLContext context;
	GLuint vertex_shader;
	GLuint fragment_shader;
	GLuint program;
	GLint ret;
	GLint width, height;

#ifdef USE_X
	XDisplay = XOpenDisplay(NULL);
	if (!XDisplay) {
		fprintf(stderr, "Error: failed to open X display.\n");
		return -1;
	}

	Window XRoot = DefaultRootWindow(XDisplay);

	XSetWindowAttributes XWinAttr;
	XWinAttr.event_mask  =  ExposureMask | PointerMotionMask;

	XWindow = XCreateWindow(XDisplay, XRoot, 0, 0, WIDTH, HEIGHT, 0,
				CopyFromParent, InputOutput,
				CopyFromParent, CWEventMask, &XWinAttr);

	XMapWindow(XDisplay, XWindow);
	XStoreName(XDisplay, XWindow, "Mali libs test");

	egl_display = eglGetDisplay((EGLNativeDisplayType) XDisplay);
#else
	egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#endif /* USE_X */
	if (egl_display == EGL_NO_DISPLAY) {
		fprintf(stderr, "Error: No display found!\n");
		return -1;
	}

	if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
		fprintf(stderr, "Error: eglInitialise failed!\n");
		return -1;
	}

	printf("EGL Version: \"%s\"\n",
	       eglQueryString(egl_display, EGL_VERSION));
	printf("EGL Vendor: \"%s\"\n",
	       eglQueryString(egl_display, EGL_VENDOR));
	printf("EGL Extensions: \"%s\"\n",
	       eglQueryString(egl_display, EGL_EXTENSIONS));

	eglChooseConfig(egl_display, config_attribute_list, &config, 1,
			&num_config);

	context = eglCreateContext(egl_display, config, EGL_NO_CONTEXT,
				   context_attribute_list);
	if (context == EGL_NO_CONTEXT) {
		fprintf(stderr, "Error: eglCreateContext failed: 0x%08X\n",
			eglGetError());
		return -1;
	}

#ifdef USE_X
	egl_surface = eglCreateWindowSurface(egl_display, config,
					     (void *) XWindow,
					     window_attribute_list);
#else
	egl_surface = eglCreateWindowSurface(egl_display, config,
					     &native_window,
					     window_attribute_list);
#endif
	if (egl_surface == EGL_NO_SURFACE) {
		fprintf(stderr, "Error: eglCreateWindowSurface failed: "
			"0x%08X\n", eglGetError());
		return -1;
	}

	if (!eglQuerySurface(egl_display, egl_surface, EGL_WIDTH, &width) ||
	    !eglQuerySurface(egl_display, egl_surface, EGL_HEIGHT, &height)) {
		fprintf(stderr, "Error: eglQuerySurface failed: 0x%08X\n",
			eglGetError());
		return -1;
	}
	printf("Surface size: %dx%d\n", width, height);

	if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, context)) {
		fprintf(stderr, "Error: eglMakeCurrent() failed: 0x%08X\n",
			eglGetError());
		return -1;
	}

	printf("GL Vendor: \"%s\"\n", glGetString(GL_VENDOR));
	printf("GL Renderer: \"%s\"\n", glGetString(GL_RENDERER));
	printf("GL Version: \"%s\"\n", glGetString(GL_VERSION));
	printf("GL Extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));

	printf("\nNow you should see the animation with 1 FPS framerate\n");
	printf("of the screen changing background color between yellow\n");
	printf("and blue (starting with yellow). Based on the timing\n");
	printf("of going fullscreen, it may expose xf86-video-mali bug.\n");

	set_fullscreen();
	Redraw(width, height);

#ifdef USE_X
	while (1) {
		Redraw(width, height);
	}
#endif

	return 0;
}
