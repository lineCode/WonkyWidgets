#include "Error.hpp"

namespace widget {

template<>
Widget* Widget::search<Widget>(const char* name) noexcept;
template<typename T>
T* Widget::search(const char* name) noexcept {
	return dynamic_cast<T*>(search<Widget>(name));
}

template<typename T>
T* Widget::search() noexcept {
	for(auto* c = mChildren; c; c = c->mNextSibling) {
		if(T* result = dynamic_cast<T*>(c)) {
			return result;
		}
		if(T* result = c->search<T>()) {
			return result;
		}
	}
	return nullptr;
}

template<typename T>
T* Widget::find(const char* name) {
	if(auto* w = search<T>(name))
		return w;
	else
		throw exceptions::WidgetNotFound(this, mName.c_str(), typeid(T).name(), name);
}

template<typename T>
T* Widget::find() {
	if(auto* w = search<T>())
		return w;
	throw exceptions::WidgetNotFound(this, mName.c_str(), typeid(T).name(), "");
}

template<>
Widget* Widget::searchParent<Widget>(const char* name) noexcept;
template<typename T>
T* Widget::searchParent(const char* name) noexcept {
	return dynamic_cast<T*>(searchParent<Widget>(name));
}
template<typename T>
T* Widget::searchParent() noexcept {
	for(Widget* p = parent(); p; p = p->parent()) {
		if(auto t = dynamic_cast<T*>(p)) {
			return t;
		}
	}
	return nullptr;
}

template<typename T>
T* Widget::findParent(const char* name) {
	if(auto* w = searchParent<T>(name))
		return w;
	throw exceptions::WidgetNotFound(this, mName.c_str(), typeid(T).name(), name);
}
template<typename T>
T* Widget::findParent() {
	if(auto* w = searchParent<T>())
		return w;
	throw exceptions::WidgetNotFound(this, mName.c_str(), typeid(T).name(), "");
}

template<typename C>
void Widget::eachChild(C&& c) {
	for(auto* w = children(); w; w = w->nextSibling()) {
		c(w);
	}
}
template<typename C>
void Widget::eachDescendendPreOrder(C&& c) {
	eachChild([&](Widget* w) {
		c(w);
		w->eachDescendendPreOrder(c);
	});
}
template<typename C>
void Widget::eachDescendendPostOrder(C&& c) {
	eachChild([&](Widget* w) {
		w->eachDescendendPostOrder(c);
		c(w);
	});
}
template<typename C>
void Widget::eachPreOrder(C&& c) {
	c(this);
	eachDescendendPreOrder(c);
}
template<typename C>
void Widget::eachPostOrder(C&& c) {
	eachDescendendPostOrder(c);
	c(this);
}

template<typename C>
void Widget::eachDescendendPreOrderConditional(C&& c) {
	eachChild([&](Widget* w) {
		if(c(w)) {
			w->eachDescendendPreOrder(c);
		}
	});
}
template<typename C>
void Widget::eachPreOrderConditional(C&& c) {
	eachDescendendPreOrderConditional(c);
	c(this);
}

template<typename C>
void Widget::eachChildConditional(C&& c) {
	for(auto* w = children(); w; w = w->nextSibling()) {
		if(!c(w)) break;
	}
}

} // namespace widget
