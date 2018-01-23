#include "../include/wwidget/Window.hpp"

#ifndef WIDGET_NO_WINDOWS

#define WIDGET_OPENGL1_IMPLEMENTATION
#include "../include/wwidget/platform/OpenGL1.hpp"

#include <GLFW/glfw3.h>

#include <stdexcept>
#include <iostream>

#define mWindow ((GLFWwindow*&) mWindowPtr)

namespace wwidget {

static int gNumWindows = 0;

static
void myGlfwErrorCallback(int level, const char* msg) {
	std::cerr << "GLFW: (" << level << "): " << msg << std::endl;
}

static
void myGlfwWindowResized(GLFWwindow* win, int width, int height) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);
	if(!window->doesShrinkFit())
		window->size(width, height);

	glfwMakeContextCurrent(win);
	glViewport(0, 0, width, height);
}

static
void myGlfwWindowPosition(GLFWwindow* win, int x, int y) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);
	// TODO: make this trigger an event
	if(window->relative()) {
		window->offset(-x, -y);
	}
}

static
void myGlfwWindowIconify(GLFWwindow* win, int iconified) {
	// Window* window = (Window*) glfwGetWindowUserPointer(win);
	int w, h;
	glfwGetWindowSize(win, &w, &h);
	myGlfwWindowResized(win, w, h);
}

static
void myGlfwCursorPosition(GLFWwindow* win, double x, double y) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);

	Dragged drag;
	drag.buttons = window->mouse().buttons;
	drag.old_x   = window->mouse().x;
	drag.old_y   = window->mouse().y;
	drag.x       = window->mouse().x = x + window->offsetx();
	drag.y       = window->mouse().y = y + window->offsety();
	drag.moved_x = drag.x - drag.old_x;
	drag.moved_y = drag.y - drag.old_y;

	if(window->mouse().buttons.any()) {
		window->send(drag);
	}
	else {
		window->send((Moved&)drag);
	}
}

static
void myGlfwClick(GLFWwindow* win, int button, int action, int mods) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);
	Click click;
	click.x      = window->mouse().x;
	click.y      = window->mouse().y;
	click.button = button;
	window->mouse().buttons[button] = action != GLFW_RELEASE;
	switch (action) {
		case GLFW_RELEASE: click.state = Event::UP; break;
		case GLFW_PRESS:   click.state = Event::DOWN; break;
		case GLFW_REPEAT:  click.state = Event::DOWN_REPEATING; break;
	}
	window->send(click);
}

static
void myGlfwScroll(GLFWwindow* win, double x, double y) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);

	Scroll scroll;
	scroll.x       = window->mouse().x;
	scroll.y       = window->mouse().y;
	// TODO: doesn't scale with dpi
	scroll.clicks_x = (float) x;
	scroll.clicks_y = (float) y;
	scroll.pixels_x = scroll.clicks_x * 16;
	scroll.pixels_y = scroll.clicks_y * 16;
	window->send(scroll);
}

static
void myGlfwKeyInput(GLFWwindow* win, int key, int scancode, int action, int mods) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);

	KeyEvent k;
	k.x = window->mouse().x;
	k.y = window->mouse().y;
	switch (action) {
		case GLFW_RELEASE: k.state = Event::UP; break;
		case GLFW_PRESS:   k.state = Event::DOWN; break;
		case GLFW_REPEAT:  k.state = Event::DOWN_REPEATING; break;
	}
	k.mods     = mods;
	k.key      = key;
	k.scancode = scancode;
	window->send(k);
}

static
void myGlfwCharInput(GLFWwindow* win, unsigned int codepoint, int mods) {
	Window* window = (Window*) glfwGetWindowUserPointer(win);

	TextInput t;
	t.mods = mods;
	t.x    = window->mouse().x;
	t.y    = window->mouse().y;

	t.utf32 = codepoint;
	t.calcUtf8();
	window->send(t);
}


Window::Window() :
	mWindowPtr(nullptr),
	mFlags(0)
{}

Window::Window(const char* title, unsigned width, unsigned height, uint32_t flags) :
	Window()
{
	open(title, width, height, flags);
}

Window::~Window() {
	clearChildren();
	close();
}

void Window::open(const char* title, unsigned width, unsigned height, uint32_t flags) {
	if(mWindow) {
		throw std::runtime_error(
			"Window already opened."
			"Window::open(\"" + std::string(title) + "\", " + std::to_string(width) + ", " + std::to_string(height) + ", " + std::to_string(flags) + ")"
		);
	}

	if(gNumWindows <= 0) {
		if(!glfwInit()) {
			throw std::runtime_error("Failed initializing glfw");
		}
		glfwSetErrorCallback(myGlfwErrorCallback);
	}

	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_DOUBLEBUFFER, ((flags & FlagDoublebuffered) != 0));
	glfwWindowHint(     GLFW_SAMPLES,  (flags & FlagAntialias) ? 4 : 0);
	glfwWindowHint(GLFW_RESIZABLE, (flags & FlagShrinkFit) == 0);
	mWindow   = glfwCreateWindow(width, height, title, NULL, NULL);
	if(!mWindow) {
		if(gNumWindows <= 0) {
			glfwTerminate();
			glfwSetErrorCallback(myGlfwErrorCallback);
		}

		throw exceptions::FailedOpeningWindow(
			"Window::open(\"" + std::string(title) + "\", " + std::to_string(width) + ", " + std::to_string(height) + ", " + std::to_string(flags) + ")"
		);
	}
	size(width, height);
	glfwMakeContextCurrent(mWindow);
	glfwSetWindowUserPointer(mWindow, this);

	glfwSetFramebufferSizeCallback(mWindow, myGlfwWindowResized);
	glfwSetWindowPosCallback(mWindow, myGlfwWindowPosition);
	glfwSetWindowIconifyCallback(mWindow, myGlfwWindowIconify);

	glfwSetCursorPosCallback(mWindow, myGlfwCursorPosition);
	glfwSetMouseButtonCallback(mWindow, myGlfwClick);
	glfwSetScrollCallback(mWindow, myGlfwScroll);
	glfwSetKeyCallback(mWindow, myGlfwKeyInput);
	glfwSetCharModsCallback(mWindow, myGlfwCharInput);

	glfwSwapInterval((flags & FlagVsync) != 0 ? 1 : 0);

	mFlags = flags;

	// TODO: don't ignore FlagAnaglyph3d
	canvas(std::make_shared<OpenGL1_Canvas>());

	++gNumWindows;
}

void Window::close() {
	if(mWindow) {
		glfwDestroyWindow(mWindow);
		--gNumWindows;
		if(gNumWindows <= 0) {
			glfwTerminate();
		}
	}
}

void Window::requestClose() {
	if(mWindow) {
		glfwSetWindowShouldClose(mWindow, GLFW_TRUE);
	}
}

bool Window::update() {
	if(mFlags & FlagUpdateOnEvent)
		glfwWaitEvents();
	else
		glfwPollEvents();

	BasicApplet::update();

	return !glfwWindowShouldClose(mWindow);
}

void Window::keepOpen() {
	while(update()) {
		draw();
	}
}

void Window::onCalcPreferredSize(PreferredSize& info) {
	if(!doesShrinkFit()) {
		int w, h;
		glfwGetWindowSize(mWindow, &w, &h);
		info.prefx = info.minx = info.maxx = w;
		info.prefy = info.miny = info.maxy = h;
	}
	else {
		Widget::onCalcPreferredSize(info);
	}
}

void Window::onResized() {
	if(doesShrinkFit()) {
		glfwSetWindowSize(mWindow, width(), height());
	}
	requestRelayout();
}

void Window::draw() {
	glfwMakeContextCurrent(mWindow);

	BasicApplet::draw();

	glfwSwapBuffers(mWindow);
}

void Window::onDrawBackground(Canvas& canvas) {
	canvas.rect({-offsetx(), -offsety(), width(), height()}, rgb(41, 41, 41));
}

void Window::onDraw(Canvas& canvas) {
	if(mFlags & FlagDrawDebug) {
		auto drawPreferredSizes = [&](auto& recurse, Widget* w) -> void {
			w->eachChild([&](Widget* c) {
				canvas.pushClipRect(c->offsetx(), c->offsety(), c->width(), c->height());
				{
					PreferredSize info;
					c->getPreferredSize(info);
					canvas.box({0, 0, c->width(), c->height()}, rgb(219, 0, 255));
					canvas.box({0, 0, info.minx,  info.miny}, rgba(255, 0, 0, 0.5f));
					canvas.rect({0, 0, info.prefx, info.prefy}, rgba(0, 255, 0, 0.1f));
					canvas.box({0, 0, info.maxx,  info.maxy}, rgba(0, 0, 255, 0.5f));
				}
				recurse(recurse, c);
				canvas.popClipRect();
			});
		};
		drawPreferredSizes(drawPreferredSizes, this);
	}
}

} // namespace wwidget

#endif // defined(WIDGET_NO_WINDOWS)
