#include "WysiwygPane.hpp"

#include "../include/wwidget/Canvas.hpp"
#include "../include/wwidget/widget/Form.hpp"

namespace wwidget {

class WysiwygMarker : public Widget {
public:
	WysiwygMarker() { align(AlignNone); }

	void copyPosition(Widget* w) {
		auto off = w->absoluteOffset(parent());
		offset(off.x, off.y);
		size(w->width(), w->height());
		preferredSizeChanged();
	}

	void onDraw(Canvas& c) override {
		// canvas.box({width(), height()}, Color::eyecancer2());
		c.strokeColor(Color::eyecancer2())
		 .rect(size())
		 .stroke();
	}

	PreferredSize onCalcPreferredSize() override {
		return size();
	}
};

class WysiwygForm : public Form {
public:
	using Form::Form;

	void onDraw(Canvas& c) override {
		c.strokeColor(rgb(235, 197, 14))
		 .rect(size())
		 .stroke();
	}
};

WysiwygPane::WysiwygPane() :
	mForm(nullptr),
	mSelected(nullptr),
	mMarker(new WysiwygMarker)
{}
WysiwygPane::~WysiwygPane() {
	delete mMarker;
}

void WysiwygPane::select(Widget *w) {
	if(mSelected == w) return;

	mSelected = w;
	if(onSelect)
		onSelect(w);

	if(!w) {
		mMarker->remove();
	}
	else {
		if(!mMarker->parent()) add(mMarker);
		mMarker->copyPosition(w);
	}
}

void WysiwygPane::load(std::string const& path) {
	unload();
	mForm = add<WysiwygForm>(path);
	mForm->align(AlignCenter);
	if(onLoaded)
		onLoaded(mForm);
}

void WysiwygPane::unload() {
	if(mForm) {
		if(onLoaded)
			onLoaded(nullptr);
		delete mForm;
		mForm = nullptr;
	}
}

void WysiwygPane::on(Dragged const& d) {
	if(d.buttons[2]) d.handled = true;
}

template<class C>
Widget* deepestChild(Widget* w, C&& callback) {
	if(!w) return nullptr;

	Widget* result = nullptr;

	Widget* child = w->children();
	while(child) {
		if(callback(child)) {
			result = child;
			child = child->children();
		}
		else {
			child = child->nextSibling();
		}
	}

	return result;
}
template<class C>
Widget* nextSibling(Widget* w, C&& callback) {
	if(!w) return nullptr;

	for(Widget* next = w->nextSibling(); next; next = next->nextSibling()) {
		if(callback(next)) {
			return next;
		}
	}

	return nullptr;
}

template<class C>
Widget* depthFirstNext(Widget* w, C&& callback) {
	if(!w || !callback(w))
		return nullptr;

	if(auto* result = nextSibling(w, callback)) return result;

	return w->parent();
}

void WysiwygPane::on(Click const& c) {
	if(c.button != 2) return;
	c.handled = true;
	if(!c.down()) return;

	auto containsCursor = [this, &c](Widget* w) -> bool {
		return Rect(w->absoluteOffset(this), w->size()).contains(c.position);
	};

	if(mSelected) {
		Widget* result = depthFirstNext(mSelected, containsCursor);
		select(result == mForm ? nullptr : result);
	}

	if(!mSelected) {
		select(deepestChild(mForm, containsCursor));
	}
}
void WysiwygPane::on(KeyEvent const& k) {
	switch (k.key) {
		default: return;
		case 256:
			if(k.state != KeyEvent::DOWN) break;
			select(nullptr);
			break;
	}
	k.handled = true;
}

} // namespace wwidget
