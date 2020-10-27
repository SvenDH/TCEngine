#include "tccore.h"
#include "tctypes.h"
#include "tclog.h"
#include "tcmath.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_KEYSTATE_BASED_INPUT
#include <nuklear.h>

/* Constants and macros: */
#ifndef MAX_KEY_QUEUE
#define MAX_KEY_QUEUE 16
#endif
/* Type declaration: */
typedef struct {
	GLFWwindow* handle;					// Native handle to the window
	const char* title;					// Title of window (shown in the bar)
	int flags;							// Window creation flags
	int state;							// Window state (chould is be closed, is it minimized etc.
	ivec2 position;						// Position of window on screen
	ivec2 display;						// Width of monitor
	ivec2 screen;						// Width of window
	ivec2 render;						// Width of frame buffer
	ivec2 offset;						// Framebuffer offset (for black bars)
	ivec2 rendersize;						// Framebuffer size
	mat3 scale;							// Scaling of pixels
} Window;

typedef struct {
	struct {
		int exitkey;					// What key is used to exit
		bool keystate[512];				// Is key pressed in current frame
		bool prevkeystate[512];			// Was ket pressed last frame
		int keyqueue[MAX_KEY_QUEUE];	// Buffer for current frame's keys pressed
		int keyqueuelen;				// Number of buffered keys
	} keyboard;
	struct {
		vec2 position;					// Position of cursor
		vec2 offset;					// Offset of mouse position
		vec2 scale;						// Scaling for mouse movement
		char buttonstate[3];			// Is mouse button clicked this frame
		char prevbuttonstate[3];		// Was a mouse button clicked last frame
		int wheelmove;					// Number of scroll wheel ticks this frame
		int prevwheelmove;				// Last frame's scroll wheel ticks
		bool onscreen;					// Is the mouse on screen
	} mouse;
} Input;

typedef struct {
	double current;						// Current (relative) time
	double prev;						// Previous frame's time
	double update;						// 
	double draw;
	double frame;
	double target;
} Time;

/* Global state: */
static Window window = { 0 };
static Input input = { 0 };
static Time time = { 0 };

/* Forward declarations: */
static bool graphicsdevice_init(int width, int height);
static void framebuffer_setup(int width, int height);
static void timer_init(void);
static void viewport_setup(int width, int height);
static void swap_buffer(void);

// GLFW callbacks:
static void error_cb(int error, const char* info);
static void windowsize_cb(GLFWwindow* window, int width, int height);
static void windowiconify_cb(GLFWwindow* window, int iconified);
static void windowfocus_cb(GLFWwindow* window, int focused);
static void windowdrop_cb(GLFWwindow* window, int count, const char** paths);
static void key_cb(GLFWwindow* window, int key, int scancode, int action, int mods);
static void char_cb(GLFWwindow* window, unsigned int key);
static void mousebutton_cb(GLFWwindow* window, int button, int action, int mods);
static void mouseposition_cb(GLFWwindow* window, double x, double y);
static void mouseenter_cb(GLFWwindow* window, int enter);
static void scroll_cb(GLFWwindow* window, double x, double y);


// Create window and initialize graphics
void window_create(int width, int height, const char* title) {
	TRACE(LOG_INFO, "[Window]: Initializing window");

	window.title = title;
	input.keyboard.exitkey = KEY_ESC;

	if (graphicsdevice_init(width, height)) window.state |= WREADY;
	else window.state &= ~WREADY;
	// If something went wrong with graphics init, abort
	if (window.state &= WREADY) window.state |= WFOCUSED;
	else return;

	timer_init();
}

void window_close(void) {
	//gfx_close();

	glfwDestroyWindow(window.handle);
	glfwTerminate();

	TRACE(LOG_INFO, "[Window]: Window closed successfully");
}

int window_alive(void) {
	if (window.state & WREADY) {
		while (!(window.flags & WCONTINUEMINIMIZED) && (window.state & WMINIMIZED)) glfwWaitEvents();
		if (glfwWindowShouldClose(window.handle)) {
			window.state |= WCLOSED;
			return false;
		}
		return true;
	}
	return false;
}

double time_get(void) {
	return glfwGetTime();
}

void inputs_poll(void) {
	input.keyboard.keyqueuelen = 0;
	// Copy current input states to previous states
	memcpy(input.keyboard.prevkeystate, input.keyboard.keystate, 512);
	memcpy(input.mouse.prevbuttonstate, input.mouse.buttonstate, 3);
	input.mouse.prevwheelmove = input.mouse.wheelmove;
	input.mouse.wheelmove = 0;

	// Register new inputs
	glfwPollEvents();
}

void swap_buffers(void) {
	glfwSwapBuffers(window.handle);
}

static bool graphicsdevice_init(int width, int height) {
	window.screen[0] = width;
	window.screen[1] = height;

	glfwSetErrorCallback(error_cb);

	if (!glfwInit()) {
		TRACE(LOG_WARNING, "[GLFW]: Failed to initialize");
		return false;
	}

	GLFWmonitor* monitor = glfwGetPrimaryMonitor();
	if (!monitor) {
		TRACE(LOG_WARNING, "[GLFW]: Failed to get primary monitor");
		return false;
	}

	GLFWvidmode* mode = glfwGetVideoMode(monitor);
	window.display[0] = mode->width;
	window.display[1] = mode->height;

	if (width <= 0) window.screen[0] = mode->width;
	if (height <= 0) window.screen[1] = mode->height;

	glfwDefaultWindowHints();

	// Resize window content area based on the monitor content scale
	glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

	// Check some Window creation flags
	if (window.flags & WHIDDEN) 
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);					// Visible window
	else 
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);					// Window initially hidden
	if (window.flags & WRESIZABLE) 
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);					// Resizable window
	else 
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);					// Avoid window being resizable
	if (window.flags & WUNDECORATED) 
		glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);					// Border and buttons on Window
	else 
		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);					// Decorated window
	if (window.flags & WTRANSPARENT) 
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);	// Transparent framebuffer
	else
		glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_FALSE);	// Opaque framebuffer

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);					// Choose OpenGL major version (just hint)
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);					// Choose OpenGL minor version (just hint)
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);	// Profiles Hint: Only 3.3 and above!
	
	int x = window.display[0] / 2 - window.screen[0] / 2;
	int y = window.display[1] / 2 - window.screen[1] / 2;
	GLFWwindow* handle;
	if (window.state & WFULLSCREEN) {
		window.position[0] = window.display[0] / 2 - window.screen[0] / 2;
		window.position[1] = window.display[1] / 2 - window.screen[1] / 2;
		// Set position to top left corner in fullscreen
		if (window.position[0] < 0) window.position[0] = 0;
		if (window.position[1] < 0) window.position[1] = 0;
		// Get closest video mode to desired size
		int count;
		GLFWvidmode* modes = glfwGetVideoModes(monitor, &count);
		for (int i = 0; i < count; i++) {
			if (modes[i].width >= window.screen[0] && modes[i].height >= window.screen[1]) {
				window.display[0] = modes[i].width;
				window.display[1] = modes[i].height;
				break;
			}
		}
		// Do not minimize when focus is lost
		if ((window.screen[0] == window.display[0]) && (window.screen[1] == window.display[1]))
			glfwWindowHint(GLFW_AUTO_ICONIFY, 0);
		
		TRACE(LOG_INFO, "[Window]: Fullscreen videomode: %i x %i", window.display[0], window.display[1]);
		
		framebuffer_setup(window.display[0], window.display[1]);

		handle = glfwCreateWindow(window.display[0], window.display[1], window.title, monitor, NULL);
	}
	else {
		handle = glfwCreateWindow(window.screen[0], window.screen[1], window.title, NULL, NULL);
		if (handle) {
			x = max(x, 0);
			y = max(y, 0);
			glfwSetWindowPos(handle, x, y);
			window.render[0] = window.screen[0];
			window.render[1] = window.screen[1];
		}
	}
	if (!handle) {
		glfwTerminate();
		TRACE(LOG_ERROR, "[GLFW]: Failed to initialize window");
		return false;
	}
	else {
		TRACE(LOG_INFO, "[Window]: Device initialized successfully:");
		TRACE(LOG_INFO, "       >> Display size:    %i x %i", window.display[0], window.display[1]);
		TRACE(LOG_INFO, "       >> Render size:     %i x %i", window.render[0], window.render[1]);
		TRACE(LOG_INFO, "       >> Screen size:     %i x %i", window.screen[0], window.screen[1]);
		TRACE(LOG_INFO, "       >> Viewport offset: %i x %i", window.offset[0], window.offset[1]);
		window.handle = handle;
	}
	glfwSetWindowSizeCallback(handle, windowsize_cb);
	glfwSetCursorEnterCallback(handle, mouseenter_cb);
	glfwSetCursorPosCallback(handle, mouseposition_cb);
	glfwSetKeyCallback(handle, key_cb);
	glfwSetCharCallback(handle, char_cb);
	glfwSetScrollCallback(handle, scroll_cb);
	glfwSetWindowIconifyCallback(handle, windowfocus_cb);
	glfwSetDropCallback(handle, windowdrop_cb);

	glfwMakeContextCurrent(handle);

	glfwSwapInterval(0);

	// Initialize GLAD extensions
	//gl_loadextensions(glfwGetProcAddress);

	if (window.flags & WVSYNC) {
		glfwSwapInterval(1);
		TRACE(LOG_INFO, "[Window]: Vsync enabled");
	}
	// Initialize graphics state
	//gfx_init(window.screen[0], window.screen[1]);
	
	// Screen scaling matrix is required in case desired screen area is different than display area
	int FBOw = window.render[0];
	int FBOh = window.render[1];
	glfwGetFramebufferSize(handle, &FBOw, &FBOh);	
	mat_transform(window.scale,0,0,0,(float)FBOw/window.screen[0],(float)FBOh/window.screen[1],0,0,0,0);
	input.mouse.scale[0] = (float)window.screen[0] / FBOw;
	input.mouse.scale[0] = (float)window.screen[1] / FBOh;

	viewport_setup(window.screen[0], window.screen[1]);
	window.rendersize[0] = window.screen[0];
	window.rendersize[1] = window.screen[1];

	//gl_clear(CLEAR_COLOR, WHITE, 0, 0);

	return true;
}

static void framebuffer_setup(int width, int height) {
	if (window.screen[0] > window.display[0] || window.screen[1] > window.screen[1]) {
		TRACE(LOG_INFO, "[Window]: Downscaling: Screen size (%ix%i) is bigger than display size (%ix%i)", 
			window.screen[0], window.screen[1], window.display[0], window.display[1]);
		float widthratio = window.display[0] / window.screen[0];
		float heightratio = window.display[1] / window.screen[1];
		if (widthratio <= heightratio) {
			float scale = (float)window.display[0] / (float)window.screen[0];
			mat_transform(window.scale,0,0,0,scale,scale,0,0,0,0);
			window.offset[0] = 0;
			window.offset[1] = window.display[1] - window.render[1];
		}
		else {
			float scale = (int)round((float)window.screen[0] * heightratio) / window.screen[0];
			mat_transform(window.scale,0,0,0,scale,scale,0,0,0,0);
			window.offset[0] = window.display[0] - window.render[0];
			window.offset[1] = 0;
		}
		window.render[0] = window.display[0];
		window.render[1] = window.display[1];
	}
	else if (window.screen[0] < window.display[0] || window.screen[1] < window.display[1]) {
		TRACE(LOG_INFO, "[Window]: Upscaling: Screen size (%ix%i) is smaller than display size (%ix%i)", 
			window.screen[0], window.screen[1], window.display[0], window.display[1]);
		float displayratio = (float)window.display[0] / (float)window.display[1];
		float screenratio = (float)window.screen[0] / (float)window.screen[1];
		if (displayratio <= screenratio) {
			window.render[0] = window.screen[0];
			window.render[1] = (int)round((float)window.screen[1] / displayratio);
			window.offset[0] = 0;
			window.offset[1] = window.render[1] - window.screen[1];
		}
		else {
			window.render[0] = (int)round((float)window.screen[0] / displayratio);
			window.render[1] = window.screen[1];
			window.offset[0] = window.render[0] - window.screen[0];
			window.offset[1] = 0;
		}
	}
	else {
		window.render[0] = window.screen[0];
		window.render[1] = window.screen[1];
		window.offset[0] = 0;
		window.offset[1] = 0;
	}
}

static void timer_init(void) {
	time.prev = time_get();
	srand((unsigned int)time.prev);              // Initialize random seed
}

static void viewport_setup(int width, int height) {
	window.render[0] = width;
	window.render[1] = height;
	irect2 viewport = { window.offset[0]/2, window.offset[1]/2, window.render[0] - window.offset[0], window.render[1] - window.offset[1] };
	//gl_viewport(viewport);

	//gfx_ortho(0, window.render[0], window.render[1], 0);
}

/* Definition of window callbacks: */

static void error_cb(int error, const char* info) {
	TRACE(LOG_WARNING, "[GLFW] %i: %s", error, info);
}

static void windowsize_cb(GLFWwindow* handle, int width, int height) {
	viewport_setup(width, height);
	window.screen[0] = width;
	window.screen[1] = height;
	window.rendersize[0] = width;
	window.rendersize[1] = height;
	window.state |= WRESIZED;
}

static void mouseenter_cb(GLFWwindow* handle, int enter) {
	input.mouse.onscreen = enter;
}

static void windowiconify_cb(GLFWwindow* handle, int iconified) {
	if (iconified) window.state |= WMINIMIZED;
	else window.state &= ~WMINIMIZED;
}

static void windowfocus_cb(GLFWwindow* handle, int focused) {
	if (focused) window.state |= WFOCUSED;
	else window.state &= ~WFOCUSED;
}

static void windowdrop_cb(GLFWwindow* handle, int count, const char** paths) {
	TRACE(LOG_WARNING, "[System]: File droping not implemented");
}

static void key_cb(GLFWwindow* handle, int key, int scancode, int action, int mods) {
	if (key == input.keyboard.exitkey && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window.handle, true);
	else {
		if (action == GLFW_RELEASE) input.keyboard.keystate[key] = false;
		else input.keyboard.keystate[key] = true;
	}
}

static void char_cb(GLFWwindow* handle, unsigned int key) {
	if (input.keyboard.keyqueuelen < MAX_KEY_QUEUE)
		input.keyboard.keyqueue[input.keyboard.keyqueuelen++] = key;
}

static void mousebutton_cb(GLFWwindow* handle, int button, int action, int mods) {
	input.mouse.buttonstate[button] = action;
}

static void mouseposition_cb(GLFWwindow* handle, double x, double y) {
	input.mouse.position[0] = x;
	input.mouse.position[1] = y;
}

static void scroll_cb(GLFWwindow* handle, double x, double y) {
	input.mouse.wheelmove = (int)y;
}