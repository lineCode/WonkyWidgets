#include "../../include/wwidget/widget/Image.hpp"

#include "../../include/wwidget/Canvas.hpp"

#include "../../include/wwidget/AttributeCollector.hpp"

namespace wwidget {

Image::Image() :
	Widget(),
	mTint(Color::white())
{}

Image::Image(std::string source) :
	Image()
{
	image(source);
}
Image::Image(Widget* addTo) : Image() {
	addTo->add(this);
}
Image::Image(Widget* addTo, std::string source) : Image(source) {
	addTo->add(this);
}

Image::~Image() {}

Image::Image(Image&& other) noexcept :
	Widget(std::move(other)),
	mSource(std::move(other.mSource)),
	mTint(std::move(other.mTint)),
	mImage(std::move(other.mImage))
{
	other.mTint = Color::white();
}
Image& Image::operator=(Image&& other) noexcept {
	Widget::operator=(std::move(other));
	mSource = std::move(other.mSource);
	mTint = other.mTint;
	other.mTint = Color::white();
	mImage = std::move(other.mImage);
	return *this;
}

void Image::reload() {
	loadImage([this](auto img) {
		if(img) {
			image(std::move(img), std::move(mSource));
		}
	}, mSource);
}

void Image::onAppletChanged() {
	mImage.reset();
	if(!source().empty() && applet())
		reload();
}
void Image::image(std::nullptr_t) {
	mSource.clear();
	mImage.reset();
}
void Image::image(std::shared_ptr<Bitmap> image, std::string source) {
	mSource = std::move(source);
	mImage  = std::move(image);
	if(mImage) {
		if(mImage->width() != width() || mImage->height() != height()) {
			preferredSizeChanged();
		}
	}
}
void Image::image(std::string const& source) {
	if(mSource == source) return;

	mSource = source;
	reload();
}
std::string const& Image::source() const noexcept {
	return mSource;
}
PreferredSize Image::onCalcPreferredSize() {
	PreferredSize result = Widget::onCalcPreferredSize();
	if(mImage) {
		result.prefx = mImage->width();
		result.prefy = mImage->height();
	}
	result.sanitize();
	return result;
}
void Image::onDrawBackground(Canvas& canvas) {
	if(!mImage) {
		canvas.rect({0, 0, width(), height()}, rgb(0, 0, 0));
		canvas.rect({0, 0, width() / 2, height() / 2}, rgb(255, 0, 255));
		canvas.rect({width() / 2, height() / 2, width() / 2, height() / 2}, rgb(255, 0, 255));
	}
}
void Image::onDraw(Canvas& canvas) {
	if(mImage) {
		canvas.rect({0, 0, width(), height()}, mImage, mTint);
	}
}
bool Image::setAttribute(std::string const& name, std::string const& value) {
	if(name == "src" || name == "source") {
		this->image(value); return true;
	}
	return Widget::setAttribute(name, value);
}
void Image::getAttributes(AttributeCollectorInterface& collector) {
	if(collector.startSection("wwidget::Image")) {
		collector("src", mSource, mSource == "");
		collector.endSection();
	}
	Widget::getAttributes(collector);
}

} // namespace wwidget
