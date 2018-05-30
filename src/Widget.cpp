#include "../include/wwidget/Widget.hpp"

#include "../include/wwidget/Context.hpp"

#include "../include/wwidget/Canvas.hpp"

#include "../include/wwidget/Error.hpp"
#include "../include/wwidget/AttributeCollector.hpp"

#include "../include/wwidget/async/OwnedTask.hpp"

#include "../include/wwidget/widget/Image.hpp"
#include "../include/wwidget/widget/Text.hpp"

#include <cstring>
#include <cmath>
#include <cassert> // assert
#include <sstream>

namespace wwidget {

Widget::Widget() noexcept :
	mPadding{0, 0, 0, 0},
	mSize(20),

	mParent(nullptr),
	mNextSibling(nullptr),
	mPrevSibling(nullptr),
	mChildren(nullptr),

	mContext(nullptr)
{
	mFlags[FlagNeedsRelayout] = true;
	mFlags[FlagChildNeedsRedraw] = true;
	mFlags[FlagNeedsRedraw] = true;
	mFlags[FlagCalcPrefSize] = true;
}

Widget::~Widget() {
	remove().release();
	clearChildrenQuietly();
}

// ** Move *******************************************************

Widget::Widget(Widget&& other) noexcept :
	Widget()
{
	*this = std::move(other);
}
Widget& Widget::operator=(Widget&& other) noexcept {
	remove();

	mName          = std::move(other.mName);
	mClasses       = std::move(other.mClasses);
	mPreferredSize = other.mPreferredSize; other.mPreferredSize = {};
	mPadding       = other.mPadding; other.mPadding = {};
	mSize          = other.mSize; other.mSize = {};
	mOffset        = other.mOffset; other.mOffset = {};
	mAlign         = other.mAlign; other.mAlign = {};
	mParent = other.mParent; other.mParent = nullptr;
	if(mParent) {
		if(mParent->mChildren == &other) {
			mParent->mChildren = this;
		}
	}
	mNextSibling = other.mNextSibling; other.mNextSibling = nullptr;
	if(mNextSibling) {
		mNextSibling->mPrevSibling = this;
	}
	mPrevSibling = other.mPrevSibling; other.mNextSibling = nullptr;
	if(mPrevSibling) {
		mPrevSibling->mNextSibling = this;
	}
	mChildren = other.mChildren; other.mChildren = nullptr;
	if(mChildren) {
		for(auto* w = children(); w; w = w->nextSibling()) {
			w->mParent = this;
		}
	}
	mContext        = other.mContext; other.mContext = nullptr;
	mFlags         = other.mFlags;
	other.mFlags = 0;
	other.mFlags[FlagNeedsRelayout] = true;
	other.mFlags[FlagChildNeedsRedraw] = true;
	other.mFlags[FlagNeedsRedraw] = true;
	other.mFlags[FlagCalcPrefSize] = true;

	return *this;
}

// ** Copy *******************************************************

Widget::Widget(Widget const& other) noexcept :
	Widget()
{
	*this = other;
}
Widget& Widget::operator=(Widget const& other) noexcept {
	mName    = other.mName; // TODO: Should the copy constructor copy the name?
	mClasses = other.mClasses;
	mFlags   = other.mFlags;
	return *this;
}

// ** Tree operations *******************************************************

void Widget::notifyChildAdded(Widget* newChild) {
	newChild->context(context());
	newChild->onAddTo(this);
	onAdd(newChild);
	if(newChild->needsRelayout()) {
		onChildPreferredSizeChanged(newChild);

		Widget* p = this;
		while(p && !p->mFlags[FlagChildNeedsRelayout]) {
			p->mFlags[FlagChildNeedsRelayout] = true;
			p = p->parent();
		}
	}
}

void Widget::notifyChildRemoved(Widget* noLongerChild) {
	noLongerChild->onRemovedFrom(this);
	onRemove(noLongerChild);
}

void Widget::add(Widget* w) {
	if(!w) {
		throw exceptions::InvalidPointer("w");
	}

	if(w->mParent) {
		if(auto ownership = w->remove()) {
			add(std::move(ownership));
			return;
		}
	}

	w->mParent  = this;
	Widget* end = lastChild();
	if(!end) {
		mChildren = w;
	}
	else {
		end->mNextSibling = w;
		w->mPrevSibling = end;
	}

	notifyChildAdded(w);
}

void Widget::add(Widget& w) {
	add(&w);
}

void Widget::add(std::initializer_list<Widget*> ptrs) {
	for(Widget* w : ptrs) add(w);
}

Widget* Widget::add(std::unique_ptr<Widget>&& w) {
	add(w.get());
	w->mFlags[FlagOwnedByParent] = true;
	return w.release();
}

Widget* Widget::insertNextSibling(Widget* w) {
	if(!mParent) {
		throw exceptions::RootNodeSibling();
	}

	if(w->mParent) {
		w->remove();
	}

	w->mNextSibling = mNextSibling;
	if(mNextSibling) {
		mNextSibling->mPrevSibling = w;
	}

	w->mPrevSibling = this;
	mNextSibling = w;

	w->mParent = mParent;

	mParent->notifyChildAdded(w);

	return w;
}

Widget* Widget::insertPrevSibling(Widget* w) {
	if(!mParent) {
		throw exceptions::RootNodeSibling();
	}

	if(w->mParent) {
		w->remove();
	}

	w->mPrevSibling = mPrevSibling;
	if(mPrevSibling) {
		mPrevSibling->mNextSibling = w;
	}
	else {
		mParent->mChildren = w;
	}

	w->mNextSibling = this;
	mPrevSibling = w;

	w->mParent = mParent;

	mParent->notifyChildAdded(w);

	return w;
}

Widget* Widget::insertAsParent(Widget* w) {
	insertNextSibling(w);
	w->add(this);
	return w;
}

std::unique_ptr<Widget> Widget::extract() {
	if(!mParent) {
		throw exceptions::InvalidOperation("Tried extracting widget without parent.");
	}

	eachChild([this](Widget* w) {
		mParent->add(w);
	});

	return remove();
}

std::unique_ptr<Widget> Widget::remove() {
	if(!mParent) return nullptr;

	auto* parent = mParent;
	auto a       = removeQuiet();
	parent->notifyChildRemoved(this);
	return a;
}

std::unique_ptr<Widget> Widget::removeQuiet() {
	removeFocus();
	if(mParent) {
		if(!mPrevSibling) {
			assert(mParent->children() == this);
			mParent->mChildren = mNextSibling;
			if(mNextSibling) {
				mNextSibling->mPrevSibling = nullptr;
			}
		}
		else {
			if(mNextSibling) {
				mNextSibling->mPrevSibling = mPrevSibling;
			}
			mPrevSibling->mNextSibling = mNextSibling;
		}

		mNextSibling = nullptr;
		mPrevSibling = nullptr;
		mParent      = nullptr;
	}
	else {
		assert(!mNextSibling);
		assert(!mPrevSibling);
	}

	return acquireOwnership();
}

std::unique_ptr<Widget> Widget::acquireOwnership() noexcept {
	if(!mFlags[FlagOwnedByParent])
		return nullptr;
	mFlags[FlagOwnedByParent] = false;
	return std::unique_ptr<Widget>(this);
}

void Widget::giveOwnershipToParent() {
	if(mFlags[FlagOwnedByParent]) throw std::runtime_error("Already owned by parent");
	if(!parent()) throw std::runtime_error("No parent to give the ownership to");
	mFlags[FlagOwnedByParent] = true;
}

template<>
Widget* Widget::search<Widget>(const char* name) noexcept {
	if(mName == name) {
		return this;
	}

	for(auto* c = mChildren; c; c = c->mNextSibling) {
		if(auto* result = c->search<Widget>(name))
			return result;
	}

	return nullptr;
}

template<>
Widget* Widget::searchParent<Widget>(const char* name) const noexcept {
	if(!mParent) return nullptr;

	Widget* p = parent();
	while(p) {
		if(p->name() == name) {
			return p;
		}
		p = p->parent();
	}

	return nullptr;
}

Widget* Widget::findRoot() const noexcept {
	Widget* root = const_cast<Widget*>(this);
	while(root->parent()) root = root->parent();
	return root;
}

void Widget::clearChildren() {
	while(mChildren) {
		mChildren->remove();
	}
}
void Widget::clearChildrenQuietly() {
	while(mChildren) {
		mChildren->removeQuiet();
	}
}

Widget* Widget::lastChild() const noexcept {
	if(!mChildren) {
		return nullptr;
	}

	Widget* w = children();
	while(w->nextSibling()) w = w->nextSibling();
	return w;
}

// Tree changed events
void Widget::onContextChanged() { }

void Widget::onAddTo(Widget* w) { }
void Widget::onRemovedFrom(Widget* parent) { }

void Widget::onAdd(Widget* w) {
	preferredSizeChanged();
	requestRelayout();
}
void Widget::onRemove(Widget* w) {
	preferredSizeChanged();
	requestRelayout();
}

// Layout events
void Widget::onResized() {
	requestRelayout();
}

void Widget::onChildPreferredSizeChanged(Widget* child) {
	preferredSizeChanged();
	requestRelayout();
}
void Widget::onChildAlignmentChanged(Widget* child) {
	AlignChild(child, {}, size());
}
PreferredSize Widget::onCalcPreferredSize() {
	return calcBoxAroundChildren(1, 1);
}
void Widget::onLayout() {
	eachChild([&](Widget* child) {
		auto& info = child->preferredSize();
		child->size(
			child->alignx() == AlignFill ? width() : info.pref.x,
			child->aligny() == AlignFill ? height() : info.pref.y
		);
		AlignChild(child, {}, size());
		ClipChild(child, {}, size());
	});
}

PreferredSize Widget::calcBoxAroundChildren(float alt_prefx, float alt_prefy) noexcept {
	PreferredSize info;
	if(!mChildren) {
		info.pref.x = (alignx() == AlignFill) ? 0 : alt_prefx;
		info.pref.y = (aligny() == AlignFill) ? 0 : alt_prefy;
	}
	else if(alignx() == AlignNone && aligny() == AlignNone) {
		info.min.x = info.max.x = info.pref.x = width();
		info.min.y = info.max.y = info.pref.y = height();
	}
	else {
		info = PreferredSize::MinMaxAccumulator();

		eachChild([&](Widget* child) {
			auto& subinfo = child->preferredSize();

			float x = (child->alignx() == AlignNone) ? std::max(0.f, child->offsetx()) : 0;
			float y = (child->aligny() == AlignNone) ? std::max(0.f, child->offsety()) : 0;
			info.include(subinfo, x, y);
		});

		info.sanitize();

		if(alignx() == AlignNone)
			info.min.x = info.max.x = info.pref.y = width();

		if(aligny() == AlignNone)
			info.min.y = info.max.y = info.pref.y = height();
	}
	return info;
}

float Widget::GetAlignmentX(Widget* child, float min, float width) noexcept {
	switch (child->alignx()) {
		case AlignNone: return child->offsetx();
		case AlignFill:
		default:
		case AlignMin: return min + child->padLeft();
		case AlignMax: return min + width - (child->width() + child->padRight());
		case AlignCenter:
			return roundf((
				min + child->padLeft() +
				(min + width - (child->width() + child->padRight()))
			) * .5f);
	}
}
float Widget::GetAlignmentY(Widget* child, float min, float height) noexcept {
	switch (child->aligny()) {
		case AlignNone: return child->offsety();
		case AlignFill:
		default:
		case AlignMin: return min + child->padTop();
		case AlignMax: return min + height - (child->height() + child->padBottom());
		case AlignCenter:
			return roundf((
				min + child->padTop() +
				(min + height - (child->height() + child->padBottom()))
			) * .5f);
	}
}
void Widget::AlignChildX(Widget* child, float min, float width) {
	child->offsetx(std::max(min, GetAlignmentX(child, min, width)));
	child->width(std::min(child->width(), width - child->offsetx()));
}
void Widget::AlignChildY(Widget* child, float min, float height) {
	child->offsety(std::max(min, GetAlignmentY(child, min, height)));
	child->height(std::min(child->height(), height - child->offsety()));
}
void Widget::AlignChild(Widget* child, Offset min, Size size) {
	child->offset(
		GetAlignmentX(child, min.x, size.x),
		GetAlignmentY(child, min.y, size.y)
	);
}
void Widget::ClipChild(Widget* child, Offset min, Size size) {
	child->offset(
		std::max(min.x, child->offsetx()),
		std::max(min.y, child->offsety())
	);
	child->size(
		std::min(child->width(),  size.x - (child->offsetx() - min.x)),
		std::min(child->height(), size.y - (child->offsety() - min.y))
	);
}

// Input events
void Widget::on(Click const& c) {
	if(!focused()) {
		c.handled = requestFocus(FOCUS_CLICK);
	}
}
void Widget::on(Scroll  const& s) { }
void Widget::on(Moved   const& c) { }
void Widget::on(Dragged const& s) { }
void Widget::on(KeyEvent  const& k) {
	if(k.scancode == 9 && focused()) {
		removeFocus();
	}
}
void Widget::on(TextInput const& t) { }

void Widget::onDescendendFocused(Rect const& area, Widget& w) {}
bool Widget::onFocus(bool b, FocusType type) { return !b; }

// Drawing events
void Widget::onDrawBackground(Canvas& graphics) {}
void Widget::onDraw(Canvas& graphics) {}

static
HalfAlignment _ParseHalfAlignment(const char* c) {
	switch(c[0]) {
		case 'f': return AlignFill;
		case 'c': return AlignCenter;
		case 'm': switch (c[1]) {
			case 'i': case 'n': return AlignMin;
			case 'x': case 'a': return AlignMax;
			default: return AlignDefault;
		}
		case 'n': case '\0': return AlignNone;
		default: return AlignDefault;
	}
}

static
Alignment _ParseAlignment(const char* c)
{
	Alignment result;
	switch (c[0]) {
		default: return _ParseHalfAlignment(c);
		case 'b': result.y = AlignMax; break;
		case 't': result.y = AlignMin; break;
		case 'f': result.y = AlignFill; break;
		case 'c': result.y = AlignCenter; break;
	}

	switch (c[1]) {
		default: return _ParseHalfAlignment(c);
		case 'l': result.x = AlignMin; break;
		case 'r': result.x = AlignMax; break;
		case 'f': result.x = AlignFill; break;
		case 'c': result.x = AlignCenter; break;
	}
	return result;
}

static
const char* _AlignmentToString(HalfAlignment a) {
	switch(a) {
		case AlignNone:   return "none";
		case AlignMin:    return "min";
		case AlignMax:    return "max";
		case AlignCenter: return "center";
		case AlignFill:   return "fill";
	}
	return "";
}

// Attributes
bool Widget::setAttribute(std::string const& s, std::string const& value) {
	if(s == "name") {
		mName.reset(value.data(), value.length()); return true;
	}
	if(s == "class") {
		classes(value); return true;
	}
	if(s == "width") {
		size(std::stof(value), height()); return true;
	}
	if(s == "height") {
		size(width(), std::stof(value)); return true;
	}
	if(s == "x") {
		offset(std::stof(value), offsety()); alignx(AlignNone); return true;
	}
	if(s == "y") {
		offset(offsetx(), std::stof(value)); aligny(AlignNone); return true;
	}
	if(s == "align") {
		align(_ParseHalfAlignment(value.c_str())); return true;
	}
	if(s == "alignx") {
		alignx(_ParseHalfAlignment(value.c_str())); return true;
	}
	if(s == "aligny") {
		aligny(_ParseHalfAlignment(value.c_str())); return true;
	}
	if(s == "padding") {
		char* cs = const_cast<char*>(value.c_str());

		// All around padding
		float a = strtof(cs, &cs);
		if(!*cs) { padding(a); return true; }
		cs++;
		if(!*cs) { padding(a); return true; }

		// X, Y padding
		float b = strtof(cs, &cs);
		if(!*cs) { padding(a, b); return true; }
		cs++;
		if(!*cs) { padding(a, b); return true; }

		float c = strtof(cs, &cs);
		if(!*cs) return false;
		cs++;
		if(!*cs) return false;

		// All around padding
		float d = strtof(cs, &cs);
		padding(a, b, c, d);

		return true;
	}
	if(s == "text") {
		text(value);
		return true;
	}
	if(s == "image") {
		image(value);
		return true;
	}

	return false;
}

void Widget::getAttributes(wwidget::AttributeCollectorInterface& collector) {
	if(!collector.startSection("wwidget::Widget")) return;

	{
		std::stringstream ss;
		ss << this;
		collector("dbg_Pointer", ss.str());
	}
	if(mFlags[FlagOwnedByParent])
		collector("dbg_FlagOwnedByParent", mFlags[FlagOwnedByParent], true);
	if(mFlags[FlagChildNeedsRelayout])
		collector("dbg_FlagChildNeedsRelayout", mFlags[FlagChildNeedsRelayout], true);
	if(mFlags[FlagNeedsRelayout])
		collector("dbg_FlagNeedsRelayout", mFlags[FlagNeedsRelayout], true);
	if(mFlags[FlagFocused])
		collector("dbg_FlagFocused", mFlags[FlagFocused], true);
	if(mFlags[FlagChildFocused])
		collector("dbg_FlagFocusedIndirectly", mFlags[FlagChildFocused], true);

	{
		auto& info = preferredSize();
		collector("dbg_MinW", info.min.x, true);
		collector("dbg_PrefW", info.pref.x, true);
		collector("dbg_MaxW", info.max.x, true);
		collector("dbg_MinH", info.min.y, true);
		collector("dbg_PrefH", info.pref.y, true);
		collector("dbg_MaxH", info.max.y, true);
	}

	collector("name", mName, mName.empty());
	{
		std::string result;
		size_t len = 0;
		for(auto& c : mClasses) len += c.length();
		result.reserve(len);
		for(auto& c : mClasses) result += c;
		collector("class", result, result.empty());
	}
	collector("width", width());
	collector("height", height());
	collector("x", offsetx(), alignx() == AlignNone);
	collector("y", offsety(), aligny() == AlignNone);
	if(alignx() == aligny()) {
		collector("align", _AlignmentToString(alignx()));
	}
	else {
		collector("alignx", _AlignmentToString(alignx()));
		collector("aligny", _AlignmentToString(aligny()));
	}

	{
		bool padx = mPadding.left == mPadding.right;
		bool pady = mPadding.top == mPadding.bottom;

		if(padx && pady) {
			if(mPadding.left == mPadding.top)
				collector("padding", mPadding.left, mPadding.left == 0);
			else
				collector("padding", mPadding.left, mPadding.top);
		}
		else {
			collector(
				"padding",
				mPadding.left, mPadding.top,
				mPadding.right, mPadding.bottom,
				false
			);
		}
	}

	// TODO: text() and image()

	collector.endSection();
}

template<typename T>
bool Widget::sendEvent(T const& t, bool skip_focused) {
	if(t.x < 0 || t.x > width() ||
		 t.y < 0 || t.y > height())
	{ return false; }

	if(!(skip_focused && focused()))
		on(t);

	eachChildConditional([&](auto* child) -> bool {
		if(t.handled) return false;
		float x = t.x;
		float y = t.y;
		t.x -= child->offsetx();
		t.y -= child->offsety();
		child->sendEvent(t, skip_focused);
		t.x = x;
		t.y = y;
		return true;
	});
	return t.handled;
};

template<typename T>
bool Widget::sendEventDepthFirst(T const& t, bool skip_focused) {
	if(t.x < 0 || t.x > width() ||
		 t.y < 0 || t.y > height())
	{ return false; }

	eachChildConditional([&](auto* child) -> bool {
		if(t.handled) return false;
		float x = t.x;
		float y = t.y;
		t.x -= child->offsetx();
		t.y -= child->offsety();
		child->sendEventDepthFirst(t, skip_focused);
		t.x = x;
		t.y = y;
		return true;
	});

	if(!t.handled && !(skip_focused && focused()))
		on(t);

	return t.handled;
}

template<typename T>
bool Widget::sendEventToFocused(T const& t) {
	if(Widget* f = findFocused()) {
		float oldx = t.x, oldy = t.y;
		float x, y;
		f->absoluteOffset(x, y, this);
		t.x -= x;
		t.y -= y;
		f->on(t);
		t.x = oldx;
		t.y = oldy;
		return true;
	}
	return false;
}

bool Widget::send(Click const& click) {
	return sendEvent(click, sendEventToFocused(click));
}
bool Widget::send(Scroll const& scroll) {
	return sendEventDepthFirst(scroll, sendEventToFocused(scroll));
}
bool Widget::send(Dragged const& drag) {
	return sendEvent(drag, sendEventToFocused(drag)) || send((Moved const&)drag);
}
bool Widget::send(Moved const& move) {
	return sendEvent(move, sendEventToFocused(move));
}
bool Widget::send(KeyEvent const& keyevent) {
	return sendEvent(keyevent, sendEventToFocused(keyevent));
}
bool Widget::send(TextInput const& character) {
	return sendEvent(character, sendEventToFocused(character));
}

void Widget::drawBackgroundRecursive(Canvas& canvas, bool minimal) {
	// TODO: don't ignore minimal
	if(minimal) {
		if(mFlags[FlagNeedsRedraw])
			onDrawBackground(canvas);
		if(!mFlags[FlagChildNeedsRedraw])
			return;
	}
	else {
		onDrawBackground(canvas);
	}

	eachChild([&](Widget* w) {
		if(w->offsetx() > -w->width() && w->offsety() > -w->height() && w->offsetx() < width() && w->offsety() < height()) {
			canvas.pushClipRect(w->offsetx(), w->offsety(), w->width(), w->height());
			w->drawBackgroundRecursive(canvas, minimal);
			canvas.popClipRect();
		}
	});
}
void Widget::drawForegroundRecursive(Canvas& canvas, bool minimal) {
	// TODO: don't ignore minimal
	// TODO: clear FlagChildNeedsRedraw and FlagNeedsRedraw
	if(!minimal || mFlags[FlagChildNeedsRedraw]) {
		eachChild([&](Widget* w) {
			if(w->offsetx() > -w->width() && w->offsety() > -w->height() && w->offsetx() < width() && w->offsety() < height()) {
				canvas.pushClipRect(w->offsetx(), w->offsety(), w->width(), w->height());
				w->drawForegroundRecursive(canvas, minimal);
				canvas.popClipRect();
			}
		});
		mFlags[FlagChildNeedsRedraw] = false;
	}

	if(minimal && !mFlags[FlagNeedsRedraw]) return;
	else mFlags[FlagNeedsRedraw] = false;

	onDraw(canvas);
}

void Widget::draw(Canvas& canvas, bool minimal) {
	updateLayout();
	canvas.pushClipRect(offsetx(), offsety(), width(), height());
	drawBackgroundRecursive(canvas, minimal);
	drawForegroundRecursive(canvas, minimal);
	canvas.popClipRect();
}

bool Widget::updateLayout() {
	bool result = false;
	if(mFlags[FlagNeedsRelayout]) {
		result = true;
		forceRelayout();
	}
	else if(mFlags[FlagChildNeedsRelayout]) {
		result = true;
		mFlags[FlagChildNeedsRelayout] = false;
		eachChild([](Widget* w) {
			w->updateLayout();
		});
	}
	return result;
}

bool Widget::forceRelayout() {
	if(!mParent) {
		auto& info = preferredSize();
		size(info.pref);
	}

	mFlags[FlagNeedsRelayout] = false;
	onLayout();

	if(!mFlags[FlagChildNeedsRelayout]) return false;

	mFlags[FlagChildNeedsRelayout] = false;
	eachChild([](Widget* w) {
		w->updateLayout();
	});
	return true;
}

void Widget::requestRelayout() {
	mFlags[FlagNeedsRelayout] = true;

	Widget* p = parent();
	while(p && !p->mFlags[FlagChildNeedsRelayout]) {
		p->mFlags[FlagChildNeedsRelayout] = true;
		p = p->parent();
	}
}

void Widget::preferredSizeChanged() {
	mFlags[FlagCalcPrefSize] = true;
	if(parent()) {
		mParent->onChildPreferredSizeChanged(this);
	}
}

void Widget::alignmentChanged() {
	if(parent()) {
		parent()->onChildAlignmentChanged(this);
	}
}

void Widget::paddingChanged() {
	preferredSizeChanged(); // TODO: is this really equal?
}


void Widget::requestRedraw() {
	if(!mFlags[FlagNeedsRedraw]) {
		for(Widget* p = parent(); p && !p->mFlags[FlagChildNeedsRedraw]; p = p->parent()) {
			if(p->mFlags[FlagChildNeedsRedraw])
				break;
			p->mFlags[FlagChildNeedsRedraw] = true;
		}
	}
}


bool Widget::clearFocus(FocusType type) {
	bool success = true;
	eachPreOrderConditional([&](Widget* w) -> bool {
		if(!w->focused() || w->childFocused()) return false;
		if(w->focused())
			success = success && w->removeFocus(type);
		return success;
	});
	return success;
}
bool Widget::requestFocus(FocusType type) {
	if(focused()) return true; // We already are focused

	if(!onFocus(true, type)) goto FAIL; // Appearently this shouldn't be focused

	if(Widget* focused_w = findRoot()->findFocused()) { // Remove existing focus
		if(!focused_w->removeFocus(type))
			goto FAIL;
	}

	mFlags[FlagFocused] = true;
	for(Widget* p = parent(); p; p = p->parent())
		p->mFlags[FlagChildFocused] = true;

	{
		Rect area = { offset(), size() };
		for(Widget* p = parent(); p; p = p->parent()) {
			p->onDescendendFocused(area, *this);
			area.min.x -= p->offsetx();
			area.min.y -= p->offsety();
			area.max.x -= p->offsetx();
			area.max.y -= p->offsety();
		}
	}

	return true;

FAIL:
		mFlags[FlagFocused] = false;
		onFocus(false, FOCUS_FORCE);
		return false;
}
bool Widget::removeFocus(FocusType type) {
	if(!focused()) return false;

	mFlags[FlagFocused] = false;
	if(!onFocus(false, type)) {
		mFlags[FlagFocused] = true;
		return false;
	}

	if(!mFlags[FlagChildFocused]) {
		Widget* p = parent();
		while(p && p->mFlags[FlagChildFocused]) {
			p->mFlags[FlagChildFocused] = false;
			if(p->focused()) break;
			p = p->parent();
		}
	}

	return true;
}

Widget* Widget::findFocused() noexcept {
	if(!mFlags[FlagChildFocused]) return nullptr;

	Widget* result = nullptr;

	eachDescendendPreOrderConditional([&](Widget* w) -> bool {
		if(result) return false;
		if(w->focused()) {
			result = w;
			return false;
		}
		if(!w->childFocused()) return false;
		return true;
	});

	return result;
}

Widget* Widget::text(std::string const& s) {
	if(auto* l = search<Text>()) {
		if(s.empty())
			l->remove();
		else
			l->content(s);
	}
	else if(!s.empty()) {
		auto* l = add<Text>();
		l->content(s)->align(AlignCenter)->classes(".generated");
	}
	return this;
}
std::string Widget::text() {
	if(auto* l = search<Text>())
		return l->content();
	else
		return "";
}
Widget* Widget::image(std::string const& s) {
	if(auto* l = search<Image>()) {
		if(s.empty())
			l->remove();
		else
			l->image(s);
	}
	else if(!s.empty()) {
		add<Image>(s)->align(AlignCenter)->classes(".generated");
	}
	return this;
}
Image* Widget::image() {
	if(auto* i = search<Image>())
		return i;
	return nullptr;
}


PreferredSize const& Widget::preferredSize() {
	if(mFlags[FlagCalcPrefSize]) {
		mFlags[FlagCalcPrefSize] = false;
		mPreferredSize = onCalcPreferredSize();
	}
	return mPreferredSize;
}

Widget* Widget::classes(
	std::string const& s) noexcept
{
	auto iter = std::lower_bound(mClasses.begin(), mClasses.end(), s.c_str());
	if(iter == mClasses.end() || *iter != s.c_str()) {
		mClasses.emplace(iter, s.data(), s.length());
	}
	return this;
}
Widget* Widget::classes(
	std::initializer_list<std::string> classes) noexcept
{
	for(auto& s : classes)
		this->classes(s);
	return this;
}

Widget* Widget::size(float w, float h) {
	return size({w, h});
}
Widget* Widget::size(Size const& size) {
	float dif = fabs(width() - size.x) + fabs(height() - size.y);
	if(dif > 1) {
		mSize = size;
		onResized();
	}
	return this;
}
Widget* Widget::width (float w) { return size(w, height()); }
Widget* Widget::height(float h) { return size(width(), h); }

Widget* Widget::offset(float x, float y) {
	return set(Offset{x, y});
}
Widget* Widget::offsetx(float x) { return offset(x, offsety()); }
Widget* Widget::offsety(float y) { return offset(offsetx(), y); }

void Widget::absoluteOffset(float& x, float& y, Widget* relativeToParent) {
	x = offsetx();
	y = offsety();
	for(Widget* p = parent(); p != relativeToParent; p = p->parent()) {
		if(p == nullptr) throw std::runtime_error("absoluteOffset: relativeTo argument is neither a nullptr nor a parent of this widget!");
		x += p->offsetx();
		y += p->offsety();
	}
}

Widget* Widget::align(Alignment a) {
	return set(a);
}
Widget* Widget::align(HalfAlignment x, HalfAlignment y) { return set(Alignment{x, y}); }
Widget* Widget::alignx(HalfAlignment x) { return set(Alignment{x, alignx()}); }
Widget* Widget::aligny(HalfAlignment y) { return set(Alignment{alignx(), y}); }

Widget* Widget::padding(float left, float top, float right, float bottom) {
	return set(Padding{left, top, right, bottom});
}
Widget* Widget::padding(float left_and_right, float top_and_bottom) {
	return set(Padding{left_and_right, top_and_bottom});
}
Widget* Widget::padding(float all_around) {
	return set(Padding{all_around});
}

// ** Set-functions *******************************************************
Widget* Widget::set(Name&& nam) {
	mName = std::move(nam);
	return this;
}
Widget* Widget::set(Class&& cls) {
	classes(cls.c_str());
	return this;
}
Widget* Widget::set(std::initializer_list<Class> clss) {
	for(auto& cls : clss) {
		classes(cls.c_str());
	}
	return this;
}

Widget* Widget::set(Padding const& pad) {
	if(pad != mPadding) {
		mPadding = pad;
		preferredSizeChanged();
	}
	return this;
}
Widget* Widget::set(Alignment const& align) {
	if(mAlign != align) {
		mAlign = align;
		alignmentChanged();
	}
	return this;
}
Widget* Widget::set(Offset const& off) {
	if(mOffset != off) {
		mOffset = off;
	}
	return this;
}
Widget* Widget::set(Size const& size) {
	if(mSize != size) {
		mSize = size;
		onResized();
	}
	return this;
}

// ** Backend shortcuts *******************************************************
Widget* Widget::context(Context* app) {
	if(mContext != app) {
		Context* oldContext = mContext;
		mContext = app;
		eachChild([&](Widget* w) {
			if(w->context() == oldContext || w->context() == nullptr) {
				w->context(mContext);
			}
		});
		onContextChanged();
	}
	return this;
}

void Widget::defer(std::function<void()> fn) {
	auto* a = context();
	if(a) {
		a->defer(std::move(fn));
	}
	else {
		// HACK: This could brake if fn() removes the widget which calls defer etc.
		// But it has to be called.
		fn();
	}
}
// void Widget::deferDraw(std::function<void()> fn) {
// 	auto* a = context();
// 	assert(a);
// 	a->deferDraw(std::move(fn));
// }

void Widget::loadImage(Owner* taskOwner, std::function<void(std::shared_ptr<Bitmap>)> fn, std::string const& url) {
	auto* a = context();
	if(!a)
		fn(nullptr);
	else
		a->loadImage(makeOwnedTask(taskOwner, std::move(fn)), url);
}
void Widget::loadImage(Owner* taskOwner, std::shared_ptr<Bitmap>& to, std::string const& url) {
	auto* a = context();
	if(!a)
		to = nullptr;
	else
		a->loadImage(makeOwnedTask(taskOwner, [&](auto p) { to = std::move(p); }), url);
}
void Widget::loadFont(Owner* taskOwner, std::function<void(std::shared_ptr<Font>)> fn, std::string const& url) {
	auto* a = context();
	if(!a)
		fn(nullptr);
	else
		a->loadFont(makeOwnedTask(taskOwner, std::move(fn)), url);
}
void Widget::loadFont(Owner* taskOwner, std::shared_ptr<Font>& to, std::string const& url) {
	auto* a = context();
	if(!a)
		to = nullptr;
	else
		a->loadFont(makeOwnedTask(taskOwner, [&](auto p) { to = std::move(p); }), url);
}

} // namespace wwidget
