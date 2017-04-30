/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2017 John Preston, https://desktop.telegram.org
*/
#include "history/history_media_types.h"

#include "lang.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "storage/localstorage.h"
#include "media/media_audio.h"
#include "media/media_clip_reader.h"
#include "media/player/media_player_instance.h"
#include "boxes/confirm_box.h"
#include "boxes/add_contact_box.h"
#include "core/click_handler_types.h"
#include "history/history_location_manager.h"
#include "window/window_controller.h"
#include "styles/style_history.h"

namespace {

constexpr auto kMaxGifForwardedBarLines = 4;

TextParseOptions _webpageTitleOptions = {
	TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _webpageDescriptionOptions = {
	TextParseLinks | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _twitterDescriptionOptions = {
	TextParseLinks | TextParseMentions | TextTwitterMentions | TextParseHashtags | TextTwitterHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _instagramDescriptionOptions = {
	TextParseLinks | TextParseMentions | TextInstagramMentions | TextParseHashtags | TextInstagramHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

inline void initTextOptions() {
	_webpageTitleOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
	_webpageTitleOptions.maxh = st::webPageTitleFont->height * 2;
	_webpageDescriptionOptions.maxw = st::msgMaxWidth - st::msgPadding.left() - st::msgPadding.right() - st::webPageLeft;
	_webpageDescriptionOptions.maxh = st::webPageDescriptionFont->height * 3;
}

bool needReSetInlineResultDocument(const MTPMessageMedia &media, DocumentData *existing) {
	if (media.type() == mtpc_messageMediaDocument) {
		if (auto document = App::feedDocument(media.c_messageMediaDocument().vdocument)) {
			if (document == existing) {
				return false;
			} else {
				document->collectLocalData(existing);
			}
		}
	}
	return true;
}

} // namespace

void historyInitMedia() {
	initTextOptions();
}

namespace {

int32 documentMaxStatusWidth(DocumentData *document) {
	int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
	if (auto song = document->song()) {
		result = qMax(result, st::normalFont->width(formatPlayedText(song->duration, song->duration)));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(song->duration, document->size)));
	} else if (auto voice = document->voice()) {
		result = qMax(result, st::normalFont->width(formatPlayedText(voice->duration, voice->duration)));
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(voice->duration, document->size)));
	} else if (document->isVideo()) {
		result = qMax(result, st::normalFont->width(formatDurationAndSizeText(document->duration(), document->size)));
	} else {
		result = qMax(result, st::normalFont->width(formatSizeText(document->size)));
	}
	return result;
}

int32 gifMaxStatusWidth(DocumentData *document) {
	int32 result = st::normalFont->width(formatDownloadText(document->size, document->size));
	result = qMax(result, st::normalFont->width(formatGifAndSizeText(document->size)));
	return result;
}

TextWithEntities captionedSelectedText(const QString &attachType, const Text &caption, TextSelection selection) {
	if (selection != FullSelection) {
		return caption.originalTextWithEntities(selection, ExpandLinksAll);
	}

	TextWithEntities result, original;
	if (!caption.isEmpty()) {
		original = caption.originalTextWithEntities(AllTextSelection, ExpandLinksAll);
	}
	result.text.reserve(5 + attachType.size() + original.text.size());
	result.text.append(qstr("[ ")).append(attachType).append(qstr(" ]"));
	if (!caption.isEmpty()) {
		result.text.append(qstr("\n"));
		appendTextWithEntities(result, std::move(original));
	}
	return result;
}

QString captionedNotificationText(const QString &attachType, const Text &caption) {
	if (caption.isEmpty()) {
		return attachType;
	}

	auto captionText = caption.originalText();
	auto attachTypeWrapped = lng_dialogs_text_media_wrapped(lt_media, attachType);
	return lng_dialogs_text_media(lt_media_part, attachTypeWrapped, lt_caption, captionText);
}

QString captionedInDialogsText(const QString &attachType, const Text &caption) {
	if (caption.isEmpty()) {
		return textcmdLink(1, textClean(attachType));
	}

	auto captionText = textClean(caption.originalText());
	auto attachTypeWrapped = textcmdLink(1, lng_dialogs_text_media_wrapped(lt_media, textClean(attachType)));
	return lng_dialogs_text_media(lt_media_part, attachTypeWrapped, lt_caption, captionText);
}

} // namespace

void HistoryFileMedia::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (p == _savel || p == _cancell) {
		if (active && !dataLoaded()) {
			ensureAnimation();
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 0., 1., st::msgFileOverDuration);
		} else if (!active && _animation) {
			_animation->a_thumbOver.start([this] { thumbAnimationCallback(); }, 1., 0., st::msgFileOverDuration);
		}
	}
}

void HistoryFileMedia::thumbAnimationCallback() {
	Ui::repaintHistoryItem(_parent);
}

void HistoryFileMedia::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	Ui::repaintHistoryItem(_parent);
}

void HistoryFileMedia::setLinks(ClickHandlerPtr &&openl, ClickHandlerPtr &&savel, ClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void HistoryFileMedia::setStatusSize(int32 newSize, int32 fullSize, int32 duration, qint64 realDuration) const {
	_statusSize = newSize;
	if (_statusSize == FileStatusSizeReady) {
		_statusText = (duration >= 0) ? formatDurationAndSizeText(duration, fullSize) : (duration < -1 ? formatGifAndSizeText(fullSize) : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeLoaded) {
		_statusText = (duration >= 0) ? formatDurationText(duration) : (duration < -1 ? qsl("GIF") : formatSizeText(fullSize));
	} else if (_statusSize == FileStatusSizeFailed) {
		_statusText = lang(lng_attach_failed);
	} else if (_statusSize >= 0) {
		_statusText = formatDownloadText(_statusSize, fullSize);
	} else {
		_statusText = formatPlayedText(-_statusSize - 1, realDuration);
	}
}

void HistoryFileMedia::step_radial(TimeMs ms, bool timer) {
	if (timer) {
		Ui::repaintHistoryItem(_parent);
	} else {
		_animation->radial.update(dataProgress(), dataFinished(), ms);
		if (!_animation->radial.animating()) {
			checkAnimationFinished();
		}
	}
}

void HistoryFileMedia::ensureAnimation() const {
	if (!_animation) {
		_animation = std::make_unique<AnimationData>(animation(const_cast<HistoryFileMedia*>(this), &HistoryFileMedia::step_radial));
	}
}

void HistoryFileMedia::checkAnimationFinished() const {
	if (_animation && !_animation->a_thumbOver.animating() && !_animation->radial.animating()) {
		if (dataLoaded()) {
			_animation.reset();
		}
	}
}

HistoryFileMedia::~HistoryFileMedia() = default;

HistoryPhoto::HistoryPhoto(HistoryItem *parent, PhotoData *photo, const QString &caption) : HistoryFileMedia(parent)
, _data(photo)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data), MakeShared<PhotoSaveClickHandler>(_data), MakeShared<PhotoCancelClickHandler>(_data));
	if (!caption.isEmpty()) {
		_caption.setText(st::messageTextStyle, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}
	init();
}

HistoryPhoto::HistoryPhoto(HistoryItem *parent, PeerData *chat, const MTPDphoto &photo, int32 width) : HistoryFileMedia(parent)
, _data(App::feedPhoto(photo)) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data, chat), MakeShared<PhotoSaveClickHandler>(_data, chat), MakeShared<PhotoCancelClickHandler>(_data, chat));

	_width = width;
	init();
}

HistoryPhoto::HistoryPhoto(HistoryItem *parent, const HistoryPhoto &other) : HistoryFileMedia(parent)
, _data(other._data)
, _pixw(other._pixw)
, _pixh(other._pixh)
, _caption(other._caption) {
	setLinks(MakeShared<PhotoOpenClickHandler>(_data), MakeShared<PhotoSaveClickHandler>(_data), MakeShared<PhotoCancelClickHandler>(_data));

	init();
}

void HistoryPhoto::init() {
	_data->thumb->load();
}

void HistoryPhoto::initDimensions() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	if (_parent->toHistoryMessage()) {
		bool bubble = _parent->hasBubble();

		int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
		int32 maxActualWidth = qMax(tw, minWidth);
		_maxw = qMax(maxActualWidth, th);
		_minh = qMax(th, int32(st::minPhotoSize));
		if (bubble) {
			maxActualWidth += st::mediaPadding.left() + st::mediaPadding.right();
			_maxw += st::mediaPadding.left() + st::mediaPadding.right();
			_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
			if (!_caption.isEmpty()) {
				auto captionw = maxActualWidth - st::msgPadding.left() - st::msgPadding.right();
				_minh += st::mediaCaptionSkip + _caption.countHeight(captionw);
				if (isBubbleBottom()) {
					_minh += st::msgPadding.bottom();
				}
			}
		}
	} else {
		_maxw = _minh = _width;
	}
}

int HistoryPhoto::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = convertScale(_data->full->width()), th = convertScale(_data->full->height());
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	if (th > st::maxMediaSize) {
		tw = (st::maxMediaSize * tw) / th;
		th = st::maxMediaSize;
	}

	_pixw = qMin(width, _maxw);
	if (bubble) {
		_pixw -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	_pixh = th;
	if (tw > _pixw) {
		_pixh = (_pixw * _pixh / tw);
	} else {
		_pixw = tw;
	}
	if (_pixh > width) {
		_pixw = (_pixw * width) / _pixh;
		_pixh = width;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;

	int minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_pixw, int16(minWidth));
	_height = qMax(_pixh, int16(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				_height += st::msgPadding.bottom();
			}
		}
	}
	return _height;
}

void HistoryPhoto::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool selected = (selection == FullSelection);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();

	bool notChild = (_parent->getMedia() == this);
	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool radial = isRadialAnimation(ms);

	auto rthumb = rtlrect(skipx, skipy, width, height, _width);
	QPixmap pix;
	if (_parent->toHistoryMessage()) {
		if (bubble) {
			skipx = st::mediaPadding.left();
			skipy = st::mediaPadding.top();

			width -= st::mediaPadding.left() + st::mediaPadding.right();
			height -= skipy + st::mediaPadding.bottom();
			if (!_caption.isEmpty()) {
				height -= st::mediaCaptionSkip + _caption.countHeight(captionw);
				if (isBubbleBottom()) {
					height -= st::msgPadding.bottom();
				}
			}
			rthumb = rtlrect(skipx, skipy, width, height, _width);
		} else {
			App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
		}
		auto inWebPage = (_parent->getMedia() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		auto roundCorners = inWebPage ? ImageRoundCorner::All : ((isBubbleTop() ? (ImageRoundCorner::TopLeft | ImageRoundCorner::TopRight) : ImageRoundCorner::None)
			| ((isBubbleBottom() && _caption.isEmpty()) ? (ImageRoundCorner::BottomLeft | ImageRoundCorner::BottomRight) : ImageRoundCorner::None));
		if (loaded) {
			pix = _data->full->pixSingle(_pixw, _pixh, width, height, roundRadius, roundCorners);
		} else {
			pix = _data->thumb->pixBlurredSingle(_pixw, _pixh, width, height, roundRadius, roundCorners);
		}
		p.drawPixmap(rthumb.topLeft(), pix);
		if (selected) {
			App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
		}
	} else {
		if (loaded) {
			pix = _data->full->pixCircled(_pixw, _pixh);
		} else {
			pix = _data->thumb->pixBlurredCircled(_pixw, _pixh);
		}
		p.drawPixmap(rthumb.topLeft(), pix);
	}
	if (radial || (!loaded && !_data->loading())) {
		float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
		QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}

		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		auto icon = ([radial, this, selected]() -> const style::icon* {
			if (radial || _data->loading()) {
				auto delayed = _data->full->toDelayedStorageImage();
				if (!delayed || !delayed->location().isNull()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		})();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		p.setOpacity(1);
		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}
	}

	// date
	if (_caption.isEmpty()) {
		if (notChild && (_data->uploading() || App::hoveredItem() == _parent)) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryPhoto::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			int captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				height -= st::msgPadding.bottom();
			}
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
				return result;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (_data->loaded()) {
			result.link = _openl;
		} else if (_data->loading()) {
			DelayedStorageImage *delayed = _data->full->toDelayedStorageImage();
			if (!delayed || !delayed->location().isNull()) {
				result.link = _cancell;
			}
		} else {
			result.link = _savel;
		}
		if (_caption.isEmpty() && _parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

void HistoryPhoto::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		auto &photo = media.c_messageMediaPhoto().vphoto;
		App::feedPhoto(photo, _data);

		if (photo.type() == mtpc_photo) {
			auto &sizes = photo.c_photo().vsizes.v;
			int32 max = 0;
			const MTPDfileLocation *maxLocation = 0;
			for (int32 i = 0, l = sizes.size(); i < l; ++i) {
				char size = 0;
				const MTPFileLocation *loc = 0;
				switch (sizes.at(i).type()) {
				case mtpc_photoSize: {
					auto &s = sizes.at(i).c_photoSize().vtype.v;
					loc = &sizes.at(i).c_photoSize().vlocation;
					if (s.size()) size = s[0];
				} break;

				case mtpc_photoCachedSize: {
					auto &s = sizes.at(i).c_photoCachedSize().vtype.v;
					loc = &sizes.at(i).c_photoCachedSize().vlocation;
					if (s.size()) size = s[0];
				} break;
				}
				if (!loc || loc->type() != mtpc_fileLocation) continue;
				if (size == 's') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->thumb);
				} else if (size == 'm') {
					Local::writeImage(storageKey(loc->c_fileLocation()), _data->medium);
				} else if (size == 'x' && max < 1) {
					max = 1;
					maxLocation = &loc->c_fileLocation();
				} else if (size == 'y' && max < 2) {
					max = 2;
					maxLocation = &loc->c_fileLocation();
					//} else if (size == 'w' && max < 3) {
					//	max = 3;
					//	maxLocation = &loc->c_fileLocation();
				}
			}
			if (maxLocation) {
				Local::writeImage(storageKey(*maxLocation), _data->full);
			}
		}
	}
}

bool HistoryPhoto::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaPhoto) {
		if (PhotoData *existing = App::feedPhoto(media.c_messageMediaPhoto().vphoto)) {
			if (existing == _data) {
				return false;
			} else {
				// collect data
			}
		}
	}
	return false;
}

void HistoryPhoto::attachToParent() {
	App::regPhotoItem(_data, _parent);
}

void HistoryPhoto::detachFromParent() {
	App::unregPhotoItem(_data, _parent);
}

QString HistoryPhoto::notificationText() const {
	return captionedNotificationText(lang(lng_in_dlg_photo), _caption);
}

QString HistoryPhoto::inDialogsText() const {
	return captionedInDialogsText(lang(lng_in_dlg_photo), _caption);
}

TextWithEntities HistoryPhoto::selectedText(TextSelection selection) const {
	return captionedSelectedText(lang(lng_in_dlg_photo), _caption, selection);
}

ImagePtr HistoryPhoto::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryVideo::HistoryVideo(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document)
, _thumbw(1)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	if (!caption.isEmpty()) {
		_caption.setText(st::messageTextStyle, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}

	setDocumentLinks(_data);

	setStatusSize(FileStatusSizeReady);

	_data->thumb->load();
}

HistoryVideo::HistoryVideo(HistoryItem *parent, const HistoryVideo &other) : HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _caption(other._caption) {
	setDocumentLinks(_data);

	setStatusSize(other._statusSize);
}

void HistoryVideo::initDimensions() {
	bool bubble = _parent->hasBubble();

	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	_thumbw = qMax(tw, 1);
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(_thumbw, int32(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			auto captionw = _maxw - st::msgPadding.left() - st::msgPadding.right();
			_minh += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				_minh += st::msgPadding.bottom();
			}
		}
	}
}

int HistoryVideo::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
	if (!tw || !th) {
		tw = th = 1;
	}
	if (tw * st::msgVideoSize.height() > th * st::msgVideoSize.width()) {
		th = qRound((st::msgVideoSize.width() / float64(tw)) * th);
		tw = st::msgVideoSize.width();
	} else {
		tw = qRound((st::msgVideoSize.height() / float64(th)) * tw);
		th = st::msgVideoSize.height();
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}

	_thumbw = qMax(tw, 1);
	int minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	minWidth = qMax(minWidth, documentMaxStatusWidth(_data) + 2 * int(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_thumbw, int(minWidth));
	_height = qMax(th, int(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			int captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				_height += st::msgPadding.bottom();
			}
		}
	}
	return _height;
}

void HistoryVideo::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int captionw = width - st::msgPadding.left() - st::msgPadding.right();

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	updateStatusText();
	bool radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				height -= st::msgPadding.bottom();
			}
		}
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	auto inWebPage = (_parent->getMedia() != this);
	auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = inWebPage ? ImageRoundCorner::All : ((isBubbleTop() ? (ImageRoundCorner::TopLeft | ImageRoundCorner::TopRight) : ImageRoundCorner::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (ImageRoundCorner::BottomLeft | ImageRoundCorner::BottomRight) : ImageRoundCorner::None));
	QRect rthumb(rtlrect(skipx, skipy, width, height, _width));
	p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, 0, width, height, roundRadius, roundCorners));
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
	p.setPen(Qt::NoPen);
	if (selected) {
		p.setBrush(st::msgDateImgBgSelected);
	} else if (isThumbAnimation(ms)) {
		auto over = _animation->a_thumbOver.current();
		p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
	} else {
		bool over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
		p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
	}

	{
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(inner);
	}

	if (!selected && _animation) {
		p.setOpacity(1);
	}

	auto icon = ([this, radial, selected, loaded]() -> const style::icon * {
		if (loaded && !radial) {
			return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
		} else if (radial || _data->loading()) {
			if (_parent->id > 0 || _data->uploading()) {
				return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
			}
			return nullptr;
		}
		return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
	})();
	if (icon) {
		icon->paintInCenter(p, inner);
	}
	if (radial) {
		QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
		_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
	}

	auto statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x(), statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
	auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
	auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
	App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
	p.setFont(st::normalFont);
	p.setPen(st::msgDateImgFg);
	p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());

	// date
	if (_caption.isEmpty()) {
		if (_parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, InfoDisplayOverImage);
		}
	} else {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryVideo::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool loaded = _data->loaded();

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			auto captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				height -= st::msgPadding.bottom();
			}
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else {
			result.link = loaded ? _openl : (_data->loading() ? _cancell : _savel);
		}
		if (_caption.isEmpty() && _parent->getMedia() == this) {
			int32 fullRight = skipx + width, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

void HistoryVideo::setStatusSize(int32 newSize) const {
	HistoryFileMedia::setStatusSize(newSize, _data->size, _data->duration(), 0);
}

QString HistoryVideo::notificationText() const {
	return captionedNotificationText(lang(lng_in_dlg_video), _caption);
}

QString HistoryVideo::inDialogsText() const {
	return captionedInDialogsText(lang(lng_in_dlg_video), _caption);
}

TextWithEntities HistoryVideo::selectedText(TextSelection selection) const {
	return captionedSelectedText(lang(lng_in_dlg_video), _caption, selection);
}

void HistoryVideo::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryVideo::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryVideo::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryVideo::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
	}
}

bool HistoryVideo::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryVideo::replyPreview() {
	if (_data->replyPreview->isNull() && !_data->thumb->isNull()) {
		if (_data->thumb->loaded()) {
			int w = convertScale(_data->thumb->width()), h = convertScale(_data->thumb->height());
			if (w <= 0) w = 1;
			if (h <= 0) h = 1;
			_data->replyPreview = ImagePtr(w > h ? _data->thumb->pix(w * st::msgReplyBarSize.height() / h, st::msgReplyBarSize.height()) : _data->thumb->pix(st::msgReplyBarSize.height()), "PNG");
		} else {
			_data->thumb->load();
		}
	}
	return _data->replyPreview;
}

HistoryDocumentCaptioned::HistoryDocumentCaptioned()
: _caption(st::msgFileMinWidth - st::msgPadding.left() - st::msgPadding.right()) {
}


HistoryDocumentVoicePlayback::HistoryDocumentVoicePlayback(const HistoryDocument *that)
: a_progress(0., 0.)
, _a_progress(animation(const_cast<HistoryDocument*>(that), &HistoryDocument::step_voiceProgress)) {
}

void HistoryDocumentVoice::ensurePlayback(const HistoryDocument *that) const {
	if (!_playback) {
		_playback = std::make_unique<HistoryDocumentVoicePlayback>(that);
	}
}

void HistoryDocumentVoice::checkPlaybackFinished() const {
	if (_playback && !_playback->_a_progress.animating()) {
		_playback.reset();
	}
}

void HistoryDocumentVoice::startSeeking() {
	_seeking = true;
	_seekingCurrent = _seekingStart;
	Media::Player::instance()->startSeeking(AudioMsgId::Type::Voice);
}

void HistoryDocumentVoice::stopSeeking() {
	_seeking = false;
	Media::Player::instance()->stopSeeking(AudioMsgId::Type::Voice);
}

HistoryDocument::HistoryDocument(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document) {
	createComponents(!caption.isEmpty());
	if (auto named = Get<HistoryDocumentNamed>()) {
		fillNamedFromData(named);
	}

	setDocumentLinks(_data);

	setStatusSize(FileStatusSizeReady);

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		captioned->_caption.setText(st::messageTextStyle, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}
}

HistoryDocument::HistoryDocument(HistoryItem *parent, const HistoryDocument &other) : HistoryFileMedia(parent)
, RuntimeComposer()
, _data(other._data) {
	auto captioned = other.Get<HistoryDocumentCaptioned>();
	createComponents(captioned != 0);
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (auto othernamed = other.Get<HistoryDocumentNamed>()) {
			named->_name = othernamed->_name;
			named->_namew = othernamed->_namew;
		} else {
			fillNamedFromData(named);
		}
	}

	setDocumentLinks(_data);

	setStatusSize(other._statusSize);

	if (captioned) {
		Get<HistoryDocumentCaptioned>()->_caption = captioned->_caption;
	}
}

void HistoryDocument::createComponents(bool caption) {
	uint64 mask = 0;
	if (_data->voice()) {
		mask |= HistoryDocumentVoice::Bit();
	} else {
		mask |= HistoryDocumentNamed::Bit();
		if (!_data->song()
			&& !documentIsExecutableName(_data->name)
			&& !_data->thumb->isNull()
			&& _data->thumb->width()
			&& _data->thumb->height()) {
			mask |= HistoryDocumentThumbed::Bit();
		}
	}
	if (caption) {
		mask |= HistoryDocumentCaptioned::Bit();
	}
	UpdateComponents(mask);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		thumbed->_linksavel = MakeShared<DocumentSaveClickHandler>(_data);
		thumbed->_linkcancell = MakeShared<DocumentCancelClickHandler>(_data);
	}
	if (auto voice = Get<HistoryDocumentVoice>()) {
		voice->_seekl = MakeShared<VoiceSeekClickHandler>(_data);
	}
}

void HistoryDocument::fillNamedFromData(HistoryDocumentNamed *named) {
	auto name = named->_name = _data->composeNameString();
	named->_namew = st::semiboldFont->width(name);
}

void HistoryDocument::initDimensions() {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (captioned && captioned->_caption.hasSkipBlock()) {
		captioned->_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	auto thumbed = Get<HistoryDocumentThumbed>();
	if (thumbed) {
		_data->thumb->load();
		int32 tw = convertScale(_data->thumb->width()), th = convertScale(_data->thumb->height());
		if (tw > th) {
			thumbed->_thumbw = (tw * st::msgFileThumbSize) / th;
		} else {
			thumbed->_thumbw = st::msgFileThumbSize;
		}
	}

	_maxw = st::msgFileMinWidth;

	int32 tleft = 0, tright = 0;
	if (thumbed) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		auto unread = _data->voice() ? (st::mediaUnreadSkip + st::mediaUnreadSize) : 0;
		_maxw = qMax(_maxw, tleft + documentMaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (auto named = Get<HistoryDocumentNamed>()) {
		_maxw = qMax(tleft + named->_namew + tright, _maxw);
		_maxw = qMin(_maxw, int(st::msgMaxWidth));
	}

	if (thumbed) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (!captioned && _parent->Has<HistoryMessageSigned>()) {
			_minh += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		_minh -= st::msgFileTopMinus;
	}

	if (captioned) {
		auto captionw = _maxw - st::msgPadding.left() - st::msgPadding.right();
		_minh += captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			_minh += st::msgPadding.bottom();
		}
	} else {
		_height = _minh;
	}
}

int HistoryDocument::resizeGetHeight(int width) {
	auto captioned = Get<HistoryDocumentCaptioned>();
	if (!captioned) {
		return HistoryFileMedia::resizeGetHeight(width);
	}

	_width = qMin(width, _maxw);
	if (Get<HistoryDocumentThumbed>()) {
		_height = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
	} else {
		_height = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		_height -= st::msgFileTopMinus;
	}
	auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
	_height += captioned->_caption.countHeight(captionw);
	if (isBubbleBottom()) {
		_height += st::msgPadding.bottom();
	}

	return _height;
}

void HistoryDocument::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	bool loaded = _data->loaded(), displayLoading = _data->displayLoading();
	bool selected = (selection == FullSelection);

	int captionw = _width - st::msgPadding.left() - st::msgPadding.right();

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	if (displayLoading) {
		ensureAnimation();
		if (!_animation->radial.animating()) {
			_animation->radial.start(_data->progress());
		}
	}
	bool showPause = updateStatusText();
	bool radial = isRadialAnimation(ms);

	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	int nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop - topMinus;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() - topMinus;

		auto inWebPage = (_parent->getMedia() != this);
		auto roundRadius = inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, _width));
		QPixmap thumb;
		if (loaded) {
			thumb = _data->thumb->pixSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize, roundRadius);
		} else {
			thumb = _data->thumb->pixBlurredSingle(thumbed->_thumbw, 0, st::msgFileThumbSize, st::msgFileThumbSize, roundRadius);
		}
		p.drawPixmap(rthumb.topLeft(), thumb);
		if (selected) {
			auto overlayCorners = inWebPage ? SelectedOverlaySmallCorners : SelectedOverlayLargeCorners;
			App::roundRect(p, rthumb, p.textPalette().selectOverlay, overlayCorners);
		}

		if (radial || (!loaded && !_data->loading())) {
			float64 radialOpacity = (radial && loaded && !_data->uploading()) ? _animation->radial.opacity() : 1;
			QRect inner(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
			p.setPen(Qt::NoPen);
			if (selected) {
				p.setBrush(st::msgDateImgBgSelected);
			} else if (isThumbAnimation(ms)) {
				auto over = _animation->a_thumbOver.current();
				p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
			} else {
				auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
				p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
			}
			p.setOpacity(radialOpacity * p.opacity());

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(inner);
			}

			p.setOpacity(radialOpacity);
			auto icon = ([radial, this, selected] {
				if (radial || _data->loading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
			})();
			p.setOpacity((radial && loaded) ? _animation->radial.opacity() : 1);
			icon->paintInCenter(p, inner);
			if (radial) {
				p.setOpacity(1);

				QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
				_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
			}
		}

		if (_data->status != FileUploadFailed) {
			const ClickHandlerPtr &lnk((_data->loading() || _data->status == FileUploading) ? thumbed->_linkcancell : thumbed->_linksavel);
			bool over = ClickHandler::showAsActive(lnk);
			p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
			p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
			p.drawTextLeft(nameleft, linktop, _width, thumbed->_link, thumbed->_linkw);
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop - topMinus;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop - topMinus;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() - topMinus;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, st::msgFileSize, st::msgFileSize, _width));
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(outbg ? st::msgFileOutBgSelected : st::msgFileInBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(outbg ? st::msgFileOutBg : st::msgFileInBg, outbg ? st::msgFileOutBgOver : st::msgFileInBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(outbg ? (over ? st::msgFileOutBgOver : st::msgFileOutBg) : (over ? st::msgFileInBgOver : st::msgFileInBg));
		}

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		if (radial) {
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			auto fg = outbg ? (selected ? st::historyFileOutRadialFgSelected : st::historyFileOutRadialFg) : (selected ? st::historyFileInRadialFgSelected : st::historyFileInRadialFg);
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, fg);
		}

		auto icon = ([showPause, radial, this, loaded, outbg, selected] {
			if (showPause) {
				return &(outbg ? (selected ? st::historyFileOutPauseSelected : st::historyFileOutPause) : (selected ? st::historyFileInPauseSelected : st::historyFileInPause));
			} else if (radial || _data->loading()) {
				return &(outbg ? (selected ? st::historyFileOutCancelSelected : st::historyFileOutCancel) : (selected ? st::historyFileInCancelSelected : st::historyFileInCancel));
			} else if (loaded) {
				if (_data->song() || _data->voice()) {
					return &(outbg ? (selected ? st::historyFileOutPlaySelected : st::historyFileOutPlay) : (selected ? st::historyFileInPlaySelected : st::historyFileInPlay));
				} else if (_data->isImage()) {
					return &(outbg ? (selected ? st::historyFileOutImageSelected : st::historyFileOutImage) : (selected ? st::historyFileInImageSelected : st::historyFileInImage));
				}
				return &(outbg ? (selected ? st::historyFileOutDocumentSelected : st::historyFileOutDocument) : (selected ? st::historyFileInDocumentSelected : st::historyFileInDocument));
			}
			return &(outbg ? (selected ? st::historyFileOutDownloadSelected : st::historyFileOutDownload) : (selected ? st::historyFileInDownloadSelected : st::historyFileInDownload));
		})();
		icon->paintInCenter(p, inner);
	}
	auto namewidth = _width - nameleft - nameright;
	auto statuswidth = namewidth;

	auto voiceStatusOverride = QString();
	if (auto voice = Get<HistoryDocumentVoice>()) {
		const VoiceWaveform *wf = nullptr;
		uchar norm_value = 0;
		if (_data->voice()) {
			wf = &_data->voice()->waveform;
			if (wf->isEmpty()) {
				wf = nullptr;
				if (loaded) {
					Local::countVoiceWaveform(_data);
				}
			} else if (wf->at(0) < 0) {
				wf = nullptr;
			} else {
				norm_value = _data->voice()->wavemax;
			}
		}
		auto progress = ([voice] {
			if (voice->seeking()) {
				return voice->seekingCurrent();
			} else if (voice->_playback) {
				return voice->_playback->a_progress.current();
			}
			return 0.;
		})();
		if (voice->seeking()) {
			voiceStatusOverride = formatPlayedText(qRound(progress * voice->_lastDurationMs) / 1000, voice->_lastDurationMs / 1000);
		}

		// rescale waveform by going in waveform.size * bar_count 1D grid
		auto active = outbg ? (selected ? st::msgWaveformOutActiveSelected : st::msgWaveformOutActive) : (selected ? st::msgWaveformInActiveSelected : st::msgWaveformInActive);
		auto inactive = outbg ? (selected ? st::msgWaveformOutInactiveSelected : st::msgWaveformOutInactive) : (selected ? st::msgWaveformInInactiveSelected : st::msgWaveformInInactive);
		auto wf_size = wf ? wf->size() : Media::Player::kWaveformSamplesCount;
		auto availw = namewidth + st::msgWaveformSkip;
		auto activew = qRound(availw * progress);
		if (!outbg && !voice->_playback && _parent->isMediaUnread()) {
			activew = availw;
		}
		auto bar_count = qMin(availw / (st::msgWaveformBar + st::msgWaveformSkip), wf_size);
		auto max_value = 0;
		auto max_delta = st::msgWaveformMax - st::msgWaveformMin;
		auto bottom = st::msgFilePadding.top() - topMinus + st::msgWaveformMax;
		p.setPen(Qt::NoPen);
		for (auto i = 0, bar_x = 0, sum_i = 0; i < wf_size; ++i) {
			auto value = wf ? wf->at(i) : 0;
			if (sum_i + bar_count >= wf_size) { // draw bar
				sum_i = sum_i + bar_count - wf_size;
				if (sum_i < (bar_count + 1) / 2) {
					if (max_value < value) max_value = value;
				}
				auto bar_value = ((max_value * max_delta) + ((norm_value + 1) / 2)) / (norm_value + 1);

				if (bar_x >= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, inactive);
				} else if (bar_x + st::msgWaveformBar <= activew) {
					p.fillRect(nameleft + bar_x, bottom - bar_value, st::msgWaveformBar, st::msgWaveformMin + bar_value, active);
				} else {
					p.fillRect(nameleft + bar_x, bottom - bar_value, activew - bar_x, st::msgWaveformMin + bar_value, active);
					p.fillRect(nameleft + activew, bottom - bar_value, st::msgWaveformBar - (activew - bar_x), st::msgWaveformMin + bar_value, inactive);
				}
				bar_x += st::msgWaveformBar + st::msgWaveformSkip;

				if (sum_i < (bar_count + 1) / 2) {
					max_value = 0;
				} else {
					max_value = value;
				}
			} else {
				if (max_value < value) max_value = value;

				sum_i += bar_count;
			}
		}
	} else if (auto named = Get<HistoryDocumentNamed>()) {
		p.setFont(st::semiboldFont);
		p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
		if (namewidth < named->_namew) {
			p.drawTextLeft(nameleft, nametop, _width, st::semiboldFont->elided(named->_name, namewidth, Qt::ElideMiddle));
		} else {
			p.drawTextLeft(nameleft, nametop, _width, named->_name, named->_namew);
		}
	}

	auto statusText = voiceStatusOverride.isEmpty() ? _statusText : voiceStatusOverride;
	auto status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, _width, statusText);

	if (_parent->isMediaUnread()) {
		auto w = st::normalFont->width(statusText);
		if (w + st::mediaUnreadSkip + st::mediaUnreadSize <= statuswidth) {
			p.setPen(Qt::NoPen);
			p.setBrush(outbg ? (selected ? st::msgFileOutBgSelected : st::msgFileOutBg) : (selected ? st::msgFileInBgSelected : st::msgFileInBg));

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(nameleft + w + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
		}
	}

	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		captioned->_caption.draw(p, st::msgPadding.left(), bottom, captionw, style::al_left, 0, -1, selection);
	}
}

HistoryTextState HistoryDocument::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool loaded = _data->loaded();

	bool showPause = updateStatusText();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0, bottom = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nameright = st::msgFileThumbPadding.left();
		nametop = st::msgFileThumbNameTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;
		bottom = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom() - topMinus;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, _width));

		if ((_data->loading() || _data->uploading() || !loaded) && rthumb.contains(x, y)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}

		if (_data->status != FileUploadFailed) {
			if (rtlrect(nameleft, linktop, thumbed->_linkw, st::semiboldFont->height, _width).contains(x, y)) {
				result.link = (_data->loading() || _data->uploading()) ? thumbed->_linkcancell : thumbed->_linksavel;
				return result;
			}
		}
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nameright = st::msgFilePadding.left();
		nametop = st::msgFileNameTop - topMinus;
		bottom = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom() - topMinus;

		QRect inner(rtlrect(st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, st::msgFileSize, st::msgFileSize, _width));
		if ((_data->loading() || _data->uploading() || !loaded) && inner.contains(x, y)) {
			result.link = (_data->loading() || _data->uploading()) ? _cancell : _savel;
			return result;
		}
	}

	if (auto voice = Get<HistoryDocumentVoice>()) {
		auto namewidth = _width - nameleft - nameright;
		auto waveformbottom = st::msgFilePadding.top() - topMinus + st::msgWaveformMax + st::msgWaveformMin;
		if (x >= nameleft && x < nameleft + namewidth && y >= nametop && y < waveformbottom) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStopped(state.state)) {
				if (!voice->seeking()) {
					voice->setSeekingStart((x - nameleft) / float64(namewidth));
				}
				result.link = voice->_seekl;
				return result;
			}
		}
	}

	int32 height = _height;
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		if (y >= bottom) {
			result = captioned->_caption.getState(x - st::msgPadding.left(), y - bottom, _width - st::msgPadding.left() - st::msgPadding.right(), request.forText());
			return result;
		}
		auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
		height -= captioned->_caption.countHeight(captionw);
		if (isBubbleBottom()) {
			height -= st::msgPadding.bottom();
		}
	}
	if (x >= 0 && y >= 0 && x < _width && y < height && !_data->loading() && !_data->uploading() && _data->isValid()) {
		result.link = _openl;
		return result;
	}
	return result;
}

void HistoryDocument::updatePressed(int x, int y) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->seeking()) {
			auto nameleft = 0, nameright = 0;
			if (auto thumbed = Get<HistoryDocumentThumbed>()) {
				nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
				nameright = st::msgFileThumbPadding.left();
			} else {
				nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
				nameright = st::msgFilePadding.left();
			}
			voice->setSeekingCurrent(snap((x - nameleft) / float64(_width - nameleft - nameright), 0., 1.));
			Ui::repaintHistoryItem(_parent);
		}
	}
}

QString HistoryDocument::notificationText() const {
	QString result;
	buildStringRepresentation([&result](const QString &type, const QString &fileName, const Text &caption) {
		result = captionedNotificationText(fileName.isEmpty() ? type : fileName, caption);
	});
	return result;
}

QString HistoryDocument::inDialogsText() const {
	QString result;
	buildStringRepresentation([&result](const QString &type, const QString &fileName, const Text &caption) {
		result = captionedInDialogsText(fileName.isEmpty() ? type : fileName, caption);
	});
	return result;
}

TextWithEntities HistoryDocument::selectedText(TextSelection selection) const {
	TextWithEntities result;
	buildStringRepresentation([&result, selection](const QString &type, const QString &fileName, const Text &caption) {
		auto fullType = type;
		if (!fileName.isEmpty()) {
			fullType.append(qstr(" : ")).append(fileName);
		}
		result = captionedSelectedText(fullType, caption, selection);
	});
	return result;
}

template <typename Callback>
void HistoryDocument::buildStringRepresentation(Callback callback) const {
	const Text emptyCaption;
	const Text *caption = &emptyCaption;
	if (auto captioned = Get<HistoryDocumentCaptioned>()) {
		caption = &captioned->_caption;
	}
	QString attachType = lang(lng_in_dlg_file);
	if (Has<HistoryDocumentVoice>()) {
		attachType = lang(lng_in_dlg_audio);
	} else if (_data->song()) {
		attachType = lang(lng_in_dlg_audio_file);
	}

	QString attachFileName;
	if (auto named = Get<HistoryDocumentNamed>()) {
		if (!named->_name.isEmpty()) {
			attachFileName = named->_name;
		}
	}
	return callback(attachType, attachFileName, *caption);
}

void HistoryDocument::setStatusSize(int32 newSize, qint64 realDuration) const {
	int32 duration = _data->song() ? _data->song()->duration : (_data->voice() ? _data->voice()->duration : -1);
	HistoryFileMedia::setStatusSize(newSize, _data->size, duration, realDuration);
	if (auto thumbed = Get<HistoryDocumentThumbed>()) {
		if (_statusSize == FileStatusSizeReady) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize == FileStatusSizeLoaded) {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		} else if (_statusSize == FileStatusSizeFailed) {
			thumbed->_link = lang(lng_media_download).toUpper();
		} else if (_statusSize >= 0) {
			thumbed->_link = lang(lng_media_cancel).toUpper();
		} else {
			thumbed->_link = lang(lng_media_open_with).toUpper();
		}
		thumbed->_linkw = st::semiboldFont->width(thumbed->_link);
	}
}

bool HistoryDocument::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		using State = Media::Player::State;
		statusSize = FileStatusSizeLoaded;
		if (_data->voice()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Voice);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStopped(state.state) && state.state != State::Finishing) {
				if (auto voice = Get<HistoryDocumentVoice>()) {
					bool was = (voice->_playback != nullptr);
					voice->ensurePlayback(this);
					if (!was || state.position != voice->_playback->_position) {
						float64 prg = state.duration ? snap(float64(state.position) / state.duration, 0., 1.) : 0.;
						if (voice->_playback->_position < state.position) {
							voice->_playback->a_progress.start(prg);
						} else {
							voice->_playback->a_progress = anim::value(0., prg);
						}
						voice->_playback->_position = state.position;
						voice->_playback->_a_progress.start();
					}
					voice->_lastDurationMs = static_cast<int>((state.duration * 1000LL) / state.frequency); // Bad :(
				}

				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.duration / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			} else {
				if (auto voice = Get<HistoryDocumentVoice>()) {
					voice->checkPlaybackFinished();
				}
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->fullId()))) {
				showPause = Media::Player::instance()->isSeeking(AudioMsgId::Type::Voice);
			}
		} else if (_data->song()) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Song);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && !Media::Player::IsStopped(state.state) && state.state != State::Finishing) {
				statusSize = -1 - (state.position / state.frequency);
				realDuration = (state.duration / state.frequency);
				showPause = (state.state == State::Playing || state.state == State::Resuming || state.state == State::Starting);
			} else {
			}
			if (!showPause && (state.id == AudioMsgId(_data, _parent->fullId()))) {
				showPause = Media::Player::instance()->isSeeking(AudioMsgId::Type::Song);
			}
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize, realDuration);
	}
	return showPause;
}

QMargins HistoryDocument::bubbleMargins() const {
	return Get<HistoryDocumentThumbed>() ? QMargins(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top(), st::msgFileThumbPadding.left(), st::msgFileThumbPadding.bottom()) : st::msgPadding;
}

void HistoryDocument::step_voiceProgress(float64 ms, bool timer) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (voice->_playback) {
			float64 dt = ms / (2 * AudioVoiceMsgUpdateView);
			if (dt >= 1) {
				voice->_playback->_a_progress.stop();
				voice->_playback->a_progress.finish();
			} else {
				voice->_playback->a_progress.update(qMin(dt, 1.), anim::linear);
			}
			if (timer) Ui::repaintHistoryItem(_parent);
		}
	}
}

void HistoryDocument::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (auto voice = Get<HistoryDocumentVoice>()) {
		if (pressed && p == voice->_seekl && !voice->seeking()) {
			voice->startSeeking();
		} else if (!pressed && voice->seeking()) {
			auto type = AudioMsgId::Type::Voice;
			auto state = Media::Player::mixer()->currentState(type);
			if (state.id == AudioMsgId(_data, _parent->fullId()) && state.duration) {
				auto currentProgress = voice->seekingCurrent();
				auto currentPosition = qRound(currentProgress * state.duration);
				Media::Player::mixer()->seek(type, currentPosition);

				voice->ensurePlayback(this);
				voice->_playback->_position = 0;
				voice->_playback->a_progress = anim::value(currentProgress, currentProgress);
			}
			voice->stopSeeking();
		}
	}
	HistoryFileMedia::clickHandlerPressedChanged(p, pressed);
}

void HistoryDocument::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryDocument::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryDocument::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
		if (!_data->data().isEmpty()) {
			if (_data->voice()) {
				Local::writeAudio(_data->mediaKey(), _data->data());
			} else {
				Local::writeStickerImage(_data->mediaKey(), _data->data());
			}
		}
	}
}

bool HistoryDocument::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryDocument::replyPreview() {
	return _data->makeReplyPreview();
}

HistoryGif::HistoryGif(HistoryItem *parent, DocumentData *document, const QString &caption) : HistoryFileMedia(parent)
, _data(document)
, _caption(st::minPhotoSize - st::msgPadding.left() - st::msgPadding.right()) {
	setDocumentLinks(_data, true);

	setStatusSize(FileStatusSizeReady);

	if (!caption.isEmpty() && !_data->isRoundVideo()) {
		_caption.setText(st::messageTextStyle, caption + _parent->skipBlock(), itemTextNoMonoOptions(_parent));
	}

	_data->thumb->load();
}

HistoryGif::HistoryGif(HistoryItem *parent, const HistoryGif &other) : HistoryFileMedia(parent)
, _data(other._data)
, _thumbw(other._thumbw)
, _thumbh(other._thumbh)
, _caption(other._caption) {
	setDocumentLinks(_data, true);

	setStatusSize(other._statusSize);
}

void HistoryGif::initDimensions() {
	if (_caption.hasSkipBlock()) {
		_caption.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	bool bubble = _parent->hasBubble();
	int32 tw = 0, th = 0;
	if (_gif && _gif->state() == Media::Clip::State::Error) {
		if (!_gif->autoplay()) {
			Ui::show(Box<InformBox>(lang(lng_gif_error)));
		}
		setClipReader(Media::Clip::ReaderPointer::Bad());
	}

	if (_gif && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}
	_thumbw = tw;
	_thumbh = th;
	_maxw = qMax(tw, int32(st::minPhotoSize));
	_minh = qMax(th, int32(st::minPhotoSize));
	_maxw = qMax(_maxw, _parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (!_gif || !_gif->ready()) {
		_maxw = qMax(_maxw, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			auto captionw = _maxw - st::msgPadding.left() - st::msgPadding.right();
			_minh += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				_minh += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		auto forwarded = _parent->Get<HistoryMessageForwarded>();
		if (forwarded) {
			forwarded->create(via);
		}
		_maxw += additionalWidth(via, reply, forwarded);
	}
}

int HistoryGif::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	int tw = 0, th = 0;
	if (_gif && _gif->ready()) {
		tw = convertScale(_gif->width());
		th = convertScale(_gif->height());
	} else {
		tw = convertScale(_data->dimensions.width()), th = convertScale(_data->dimensions.height());
		if (!tw || !th) {
			tw = convertScale(_data->thumb->width());
			th = convertScale(_data->thumb->height());
		}
	}
	if (tw > st::maxGifSize) {
		th = (st::maxGifSize * th) / tw;
		tw = st::maxGifSize;
	}
	if (th > st::maxGifSize) {
		tw = (st::maxGifSize * tw) / th;
		th = st::maxGifSize;
	}
	if (!tw || !th) {
		tw = th = 1;
	}

	if (bubble) {
		width -= st::mediaPadding.left() + st::mediaPadding.right();
	}
	if (width < tw) {
		th = qRound((width / float64(tw)) * th);
		tw = width;
	}
	_thumbw = tw;
	_thumbh = th;

	_width = qMax(tw, int32(st::minPhotoSize));
	_height = qMax(th, int32(st::minPhotoSize));
	_width = qMax(_width, _parent->infoWidth() + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	if (_gif && _gif->ready()) {
		if (!_gif->started()) {
			auto isRound = _data->isRoundVideo();
			auto inWebPage = (_parent->getMedia() != this);
			auto roundRadius = isRound ? ImageRoundRadius::Ellipse : inWebPage ? ImageRoundRadius::Small : ImageRoundRadius::Large;
			auto roundCorners = (isRound || inWebPage) ? ImageRoundCorner::All : ((isBubbleTop() ? (ImageRoundCorner::TopLeft | ImageRoundCorner::TopRight) : ImageRoundCorner::None)
				| ((isBubbleBottom() && _caption.isEmpty()) ? (ImageRoundCorner::BottomLeft | ImageRoundCorner::BottomRight) : ImageRoundCorner::None));
			_gif->start(_thumbw, _thumbh, _width, _height, roundRadius, roundCorners);
			if (isRound) {
				Media::Player::mixer()->setVideoVolume(1.);
			}
		}
	} else {
		_width = qMax(_width, gifMaxStatusWidth(_data) + 2 * int32(st::msgDateImgDelta + st::msgDateImgPadding.x()));
	}
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			auto captionw = _width - st::msgPadding.left() - st::msgPadding.right();
			_height += st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				_height += st::msgPadding.bottom();
			}
		}
	} else if (isSeparateRoundVideo()) {
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		auto forwarded = _parent->Get<HistoryMessageForwarded>();
		if (via || reply || forwarded) {
			auto additional = additionalWidth(via, reply, forwarded);
			_width += additional;
			accumulate_min(_width, width);
			auto usew = _maxw - additional;
			auto availw = _width - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (!forwarded && via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}

	return _height;
}

void HistoryGif::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->automaticLoad(_parent);
	auto loaded = _data->loaded();
	auto displayLoading = (_parent->id < 0) || _data->displayLoading();
	auto selected = (selection == FullSelection);

	auto videoFinished = _gif && (_gif->mode() == Media::Clip::Reader::Mode::Video) && (_gif->state() == Media::Clip::State::Finished);
	if (loaded && cAutoPlayGif() && ((!_gif && !_gif.isBad()) || videoFinished)) {
		Ui::autoplayMediaInlineAsync(_parent->fullId());
	}

	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	auto isChildMedia = (_parent->getMedia() != this);

	auto captionw = width - st::msgPadding.left() - st::msgPadding.right();

	auto isRound = _data->isRoundVideo();
	auto displayMute = false;
	auto animating = (_gif && _gif->started());

	if (!animating || _parent->id < 0) {
		if (displayLoading) {
			ensureAnimation();
			if (!_animation->radial.animating()) {
				_animation->radial.start(dataProgress());
			}
		}
		updateStatusText();
	} else if (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video) {
		updateStatusText();
	}
	auto radial = isRadialAnimation(ms);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
		if (!_caption.isEmpty()) {
			height -= st::mediaCaptionSkip + _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				height -= st::msgPadding.bottom();
			}
		}
	} else if (!isRound) {
		App::roundShadow(p, 0, 0, width, _height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	auto usex = 0, usew = width;
	auto via = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageReply>();
	auto forwarded = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageForwarded>();
	if (via || reply || forwarded) {
		usew = _maxw - additionalWidth(via, reply, forwarded);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	QRect rthumb(rtlrect(usex + skipx, skipy, usew, height, _width));

	auto roundRadius = isRound ? ImageRoundRadius::Ellipse : isChildMedia ? ImageRoundRadius::Small : ImageRoundRadius::Large;
	auto roundCorners = (isRound || isChildMedia) ? ImageRoundCorner::All : ((isBubbleTop() ? (ImageRoundCorner::TopLeft | ImageRoundCorner::TopRight) : ImageRoundCorner::None)
		| ((isBubbleBottom() && _caption.isEmpty()) ? (ImageRoundCorner::BottomLeft | ImageRoundCorner::BottomRight) : ImageRoundCorner::None));
	if (animating) {
		auto paused = App::wnd()->controller()->isGifPausedAtLeastFor(Window::GifPauseReason::Any);
		if (isRound) {
			if (_gif->mode() == Media::Clip::Reader::Mode::Video) {
				paused = false;
			} else {
				displayMute = true;
			}
		}
		p.drawPixmap(rthumb.topLeft(), _gif->current(_thumbw, _thumbh, usew, height, roundRadius, roundCorners, paused ? 0 : ms));
	} else {
		p.drawPixmap(rthumb.topLeft(), _data->thumb->pixBlurredSingle(_thumbw, _thumbh, usew, height, roundRadius, roundCorners));
	}
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (radial || _gif.isBad() || (!_gif && ((!loaded && !_data->loading()) || !cAutoPlayGif()))) {
		auto radialOpacity = (radial && loaded && _parent->id > 0) ? _animation->radial.opacity() : 1.;
		auto inner = QRect(rthumb.x() + (rthumb.width() - st::msgFileSize) / 2, rthumb.y() + (rthumb.height() - st::msgFileSize) / 2, st::msgFileSize, st::msgFileSize);
		p.setPen(Qt::NoPen);
		if (selected) {
			p.setBrush(st::msgDateImgBgSelected);
		} else if (isThumbAnimation(ms)) {
			auto over = _animation->a_thumbOver.current();
			p.setBrush(anim::brush(st::msgDateImgBg, st::msgDateImgBgOver, over));
		} else {
			auto over = ClickHandler::showAsActive(_data->loading() ? _cancell : _savel);
			p.setBrush(over ? st::msgDateImgBgOver : st::msgDateImgBg);
		}
		p.setOpacity(radialOpacity * p.opacity());

		{
			PainterHighQualityEnabler hq(p);
			p.drawEllipse(inner);
		}

		p.setOpacity(radialOpacity);
		auto icon = ([this, radial, selected]() -> const style::icon * {
			if (_data->loaded() && !radial) {
				return &(selected ? st::historyFileThumbPlaySelected : st::historyFileThumbPlay);
			} else if (radial || _data->loading()) {
				if (_parent->id > 0 || _data->uploading()) {
					return &(selected ? st::historyFileThumbCancelSelected : st::historyFileThumbCancel);
				}
				return nullptr;
			}
			return &(selected ? st::historyFileThumbDownloadSelected : st::historyFileThumbDownload);
		})();
		if (icon) {
			icon->paintInCenter(p, inner);
		}
		if (radial) {
			p.setOpacity(1);
			QRect rinner(inner.marginsRemoved(QMargins(st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine, st::msgFileRadialLine)));
			_animation->radial.draw(p, rinner, st::msgFileRadialLine, selected ? st::historyFileThumbRadialFgSelected : st::historyFileThumbRadialFg);
		}

		if (!isRound && (!animating || _parent->id < 0)) {
			auto statusX = skipx + st::msgDateImgDelta + st::msgDateImgPadding.x();
			auto statusY = skipy + st::msgDateImgDelta + st::msgDateImgPadding.y();
			auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
			auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
			App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);
			p.setFont(st::normalFont);
			p.setPen(st::msgDateImgFg);
			p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		}
	}
	if (displayMute) {
		auto muteRect = rtlrect(rthumb.x() + (rthumb.width() - st::historyVideoMessageMuteSize) / 2, rthumb.y() + st::msgDateImgDelta, st::historyVideoMessageMuteSize, st::historyVideoMessageMuteSize, _width);
		p.setPen(Qt::NoPen);
		p.setBrush(selected ? st::msgDateImgBgSelected : st::msgDateImgBg);
		PainterHighQualityEnabler hq(p);
		p.drawEllipse(muteRect);
		(selected ? st::historyVideoMessageMuteSelected : st::historyVideoMessageMute).paintInCenter(p, muteRect);
	}

	if (isRound) {
		auto mediaUnread = _parent->isMediaUnread();
		auto statusW = st::normalFont->width(_statusText) + 2 * st::msgDateImgPadding.x();
		auto statusH = st::normalFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = usex + skipx + st::msgDateImgDelta + st::msgDateImgPadding.x();
		auto statusY = skipy + height - st::msgDateImgDelta - statusH + st::msgDateImgPadding.y();
		if (_parent->isMediaUnread()) {
			statusW += st::mediaUnreadSkip + st::mediaUnreadSize;
		}
		App::roundRect(p, rtlrect(statusX - st::msgDateImgPadding.x(), statusY - st::msgDateImgPadding.y(), statusW, statusH, _width), selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
		p.setFont(st::normalFont);
		p.setPen(st::msgServiceFg);
		p.drawTextLeft(statusX, statusY, _width, _statusText, statusW - 2 * st::msgDateImgPadding.x());
		if (mediaUnread) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::msgServiceFg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(rtlrect(statusX - st::msgDateImgPadding.x() + statusW - st::msgDateImgPadding.x() - st::mediaUnreadSize, statusY + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
		}
		if (!isChildMedia && (via || reply || forwarded)) {
			auto rectw = _width - usew - st::msgReplyPadding.left();
			auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
			auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			auto forwardedHeightReal = forwarded ? forwarded->_text.countHeight(innerw) : 0;
			auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
			if (forwarded) {
				recth += forwardedHeight;
			} else if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
			int recty = skipy;
			if (rtl()) rectx = _width - rectx - rectw;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw = innerw;
			if (forwarded) {
				p.setTextPalette(st::serviceTextPalette);
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				forwarded->_text.drawElided(p, rectx, recty + st::msgReplyPadding.top(), rectw, kMaxGifForwardedBarLines, style::al_left, 0, -1, 0, breakEverywhere);
				p.restoreTextPalette();
			} else if (via) {
				p.setFont(st::msgDateFont);
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->_text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				HistoryMessageReply::PaintFlags flags = 0;
				if (selected) {
					flags |= HistoryMessageReply::PaintSelected;
				}
				reply->paint(p, _parent, rectx, recty, rectw, flags);
			}
		}
	}
	if (!_caption.isEmpty()) {
		p.setPen(outbg ? (selected ? st::historyTextOutFgSelected : st::historyTextOutFg) : (selected ? st::historyTextInFgSelected : st::historyTextInFg));
		_caption.draw(p, st::msgPadding.left(), skipy + height + st::mediaPadding.bottom() + st::mediaCaptionSkip, captionw, style::al_left, 0, -1, selection);
	} else if (!isChildMedia && (isRound || _data->uploading() || App::hoveredItem() == _parent)) {
		auto fullRight = skipx + usex + usew;
		auto fullBottom = skipy + height;
		if (isRound && !outbg) {
			auto infoWidth = _parent->infoWidth();

			// This is just some arbitrary point,
			// the main idea is to make info left aligned here.
			fullRight += infoWidth - st::normalFont->height;

			auto maxRight = _parent->history()->width - st::msgMargin.left();
			if (_parent->history()->canHaveFromPhotos()) {
				maxRight -= st::msgMargin.right();
			} else {
				maxRight -= st::msgMargin.left();
			}
			if (fullRight > maxRight) {
				fullRight = maxRight;
			}
		}
		_parent->drawInfo(p, fullRight, fullBottom, 2 * skipx + width, selected, isRound ? InfoDisplayOverBackground : InfoDisplayOverImage);
	}
}

HistoryTextState HistoryGif::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();
		if (!_caption.isEmpty()) {
			auto captionw = width - st::msgPadding.left() - st::msgPadding.right();
			height -= _caption.countHeight(captionw);
			if (isBubbleBottom()) {
				height -= st::msgPadding.bottom();
			}
			if (x >= st::msgPadding.left() && y >= height && x < st::msgPadding.left() + captionw && y < _height) {
				result = _caption.getState(x - st::msgPadding.left(), y - height, captionw, request.forText());
				return result;
			}
			height -= st::mediaCaptionSkip;
		}
		width -= st::mediaPadding.left() + st::mediaPadding.right();
		height -= skipy + st::mediaPadding.bottom();
	}
	auto out = _parent->out(), isPost = _parent->isPost();
	auto isChildMedia = (_parent->getMedia() != this);
	auto isRound = _data->isRoundVideo();
	auto usew = width, usex = 0;
	auto via = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageReply>();
	auto forwarded = (!isRound || isChildMedia) ? nullptr : _parent->Get<HistoryMessageForwarded>();
	if (via || reply || forwarded) {
		usew = _maxw - additionalWidth(via, reply, forwarded);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (via || reply || forwarded) {
		auto rectw = width - usew - st::msgReplyPadding.left();
		auto innerw = rectw - (st::msgReplyPadding.left() + st::msgReplyPadding.right());
		auto recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		auto forwardedHeightReal = forwarded ? forwarded->_text.countHeight(innerw) : 0;
		auto forwardedHeight = qMin(forwardedHeightReal, kMaxGifForwardedBarLines * st::msgServiceNameFont->height);
		if (forwarded) {
			recth += forwardedHeight;
		} else if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		auto rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
		auto recty = skipy;
		if (rtl()) rectx = _width - rectx - rectw;

		if (forwarded) {
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + st::msgReplyPadding.top() + forwardedHeight) {
				auto breakEverywhere = (forwardedHeightReal > forwardedHeight);
				auto textRequest = request.forText();
				if (breakEverywhere) {
					textRequest.flags |= Text::StateRequest::Flag::BreakEverywhere;
				}
				result = forwarded->_text.getState(x - rectx - st::msgReplyPadding.left(), y - recty - st::msgReplyPadding.top(), innerw, textRequest);
				result.symbol = 0;
				result.afterSymbol = false;
				if (breakEverywhere) {
					result.cursor = HistoryInForwardedCursorState;
				} else {
					result.cursor = HistoryDefaultCursorState;
				}
				return result;
			}
			recty += forwardedHeight;
			recth -= forwardedHeight;
		} else if (via) {
			int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + viah) {
				result.link = via->_lnk;
				return result;
			}
			int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + recth) {
				result.link = reply->replyToLink();
				return result;
			}
		}
	}
	if (x >= usex + skipx && y >= skipy && x < usex + skipx + usew && y < skipy + height) {
		if (_data->uploading()) {
			result.link = _cancell;
		} else if (!_gif || !cAutoPlayGif() || _data->isRoundVideo()) {
			result.link = _data->loaded() ? _openl : (_data->loading() ? _cancell : _savel);
		}
		if (!isChildMedia) {
			int32 fullRight = usex + skipx + usew, fullBottom = skipy + height;
			bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
			if (inDate) {
				result.cursor = HistoryInDateCursorState;
			}
		}
		return result;
	}
	return result;
}

QString HistoryGif::notificationText() const {
	return captionedNotificationText(mediaTypeString(), _caption);
}

QString HistoryGif::inDialogsText() const {
	return captionedInDialogsText(mediaTypeString(), _caption);
}

TextWithEntities HistoryGif::selectedText(TextSelection selection) const {
	return captionedSelectedText(mediaTypeString(), _caption, selection);
}

QString HistoryGif::mediaTypeString() const {
	return _data->isRoundVideo() ? lang(lng_in_dlg_video_message) : qsl("GIF");
}

void HistoryGif::setStatusSize(int32 newSize) const {
	if (_data->isRoundVideo()) {
		_statusSize = newSize;
		if (newSize < 0) {
			_statusText = formatDurationText(-newSize - 1);
		} else {
			_statusText = formatDurationText(_data->duration());
		}
	} else {
		HistoryFileMedia::setStatusSize(newSize, _data->size, -2, 0);
	}
}

void HistoryGif::updateStatusText() const {
	bool showPause = false;
	int32 statusSize = 0, realDuration = 0;
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = FileStatusSizeFailed;
	} else if (_data->status == FileUploading) {
		statusSize = _data->uploadOffset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (_data->loaded()) {
		statusSize = FileStatusSizeLoaded;
		if (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video) {
			auto state = Media::Player::mixer()->currentState(AudioMsgId::Type::Video);
			statusSize = -1 - (state.position / state.frequency);
		}
	} else {
		statusSize = FileStatusSizeReady;
	}
	if (statusSize != _statusSize) {
		setStatusSize(statusSize);
	}
}

void HistoryGif::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistoryGif::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistoryGif::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
	}
}

bool HistoryGif::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistoryGif::replyPreview() {
	return _data->makeReplyPreview();
}

int HistoryGif::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply, const HistoryMessageForwarded *forwarded) const {
	int result = 0;
	if (forwarded) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + forwarded->_text.maxWidth() + st::msgReplyPadding.right());
	} else if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->_maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

bool HistoryGif::playInline(bool autoplay) {
	using Mode = Media::Clip::Reader::Mode;
	if (_data->isRoundVideo() && _gif) {
		// Stop autoplayed silent video when we start playback by click.
		// Stop finished video message when autoplay starts.
		if ((!autoplay && _gif->mode() == Mode::Gif)
			|| (autoplay && _gif->mode() == Mode::Video && _gif->state() == Media::Clip::State::Finished)) {
			stopInline();
		}
	}
	if (_gif) {
		stopInline();
	} else if (_data->loaded(DocumentData::FilePathResolveChecked)) {
		if (!cAutoPlayGif()) {
			App::stopGifItems();
		}
		auto mode = (!autoplay && _data->isRoundVideo()) ? Mode::Video : Mode::Gif;
		setClipReader(Media::Clip::MakeReader(_data->location(), _data->data(), [this](Media::Clip::Notification notification) {
			_parent->clipCallback(notification);
		}, mode));
		if (mode == Mode::Video) {
			if (App::main()) {
				App::main()->mediaMarkRead(_data);
			}
			App::wnd()->controller()->enableGifPauseReason(Window::GifPauseReason::RoundPlaying);
		}
		if (_gif && autoplay) {
			_gif->setAutoplay();
		}
	}
	return true;
}

void HistoryGif::stopInline() {
	if (_gif && _gif->mode() == Media::Clip::Reader::Mode::Video) {
		App::wnd()->controller()->disableGifPauseReason(Window::GifPauseReason::RoundPlaying);
	}
	clearClipReader();

	_parent->setPendingInitDimensions();
	Notify::historyItemLayoutChanged(_parent);
}

void HistoryGif::setClipReader(Media::Clip::ReaderPointer gif) {
	if (_gif) {
		App::unregGifItem(_gif.get());
	}
	_gif = std::move(gif);
	if (_gif) {
		App::regGifItem(_gif.get(), _parent);
	}
}

HistoryGif::~HistoryGif() {
	clearClipReader();
}

float64 HistoryGif::dataProgress() const {
	return (_data->uploading() || !_parent || _parent->id > 0) ? _data->progress() : 0;
}

bool HistoryGif::dataFinished() const {
	return (!_parent || _parent->id > 0) ? (!_data->loading() && !_data->uploading()) : false;
}

bool HistoryGif::dataLoaded() const {
	return (!_parent || _parent->id > 0) ? _data->loaded() : false;
}

HistorySticker::HistorySticker(HistoryItem *parent, DocumentData *document) : HistoryMedia(parent)
, _data(document)
, _emoji(_data->sticker()->alt) {
	_data->thumb->load();
	if (auto emoji = Ui::Emoji::Find(_emoji)) {
		_emoji = emoji->text();
	}
}

void HistorySticker::initDimensions() {
	auto sticker = _data->sticker();

	if (!_packLink && sticker && sticker->set.type() != mtpc_inputStickerSetEmpty) {
		_packLink = MakeShared<LambdaClickHandler>([document = _data] {
			if (auto sticker = document->sticker()) {
				if (sticker->set.type() != mtpc_inputStickerSetEmpty && App::main()) {
					App::main()->stickersBox(sticker->set);
				}
			}
		});
	}
	_pixw = _data->dimensions.width();
	_pixh = _data->dimensions.height();
	if (_pixw > st::maxStickerSize) {
		_pixh = (st::maxStickerSize * _pixh) / _pixw;
		_pixw = st::maxStickerSize;
	}
	if (_pixh > st::maxStickerSize) {
		_pixw = (st::maxStickerSize * _pixw) / _pixh;
		_pixh = st::maxStickerSize;
	}
	if (_pixw < 1) _pixw = 1;
	if (_pixh < 1) _pixh = 1;
	_maxw = qMax(_pixw, int16(st::minPhotoSize));
	_minh = qMax(_pixh, int16(st::minPhotoSize));
	if (_parent->getMedia() == this) {
		_maxw += additionalWidth();
	}

	_height = _minh;
}

int HistorySticker::resizeGetHeight(int width) { // return new height
	_width = qMin(width, _maxw);
	if (_parent->getMedia() == this) {
		auto via = _parent->Get<HistoryMessageVia>();
		auto reply = _parent->Get<HistoryMessageReply>();
		if (via || reply) {
			int usew = _maxw - additionalWidth(via, reply);
			int availw = _width - usew - st::msgReplyPadding.left() - st::msgReplyPadding.left() - st::msgReplyPadding.left();
			if (via) {
				via->resize(availw);
			}
			if (reply) {
				reply->resize(availw);
			}
		}
	}
	return _height;
}

void HistorySticker::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	auto sticker = _data->sticker();
	if (!sticker) return;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;

	_data->checkSticker();
	bool loaded = _data->loaded();
	bool selected = (selection == FullSelection);

	bool out = _parent->out(), isPost = _parent->isPost(), childmedia = (_parent->getMedia() != this);

	int usew = _maxw, usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (selected) {
		if (sticker->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurredColored(st::msgStickerOverlay, _pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), sticker->img->pixColored(st::msgStickerOverlay, _pixw, _pixh));
		}
	} else {
		if (sticker->img->isNull()) {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), _data->thumb->pixBlurred(_pixw, _pixh));
		} else {
			p.drawPixmap(QPoint(usex + (usew - _pixw) / 2, (_minh - _pixh) / 2), sticker->img->pix(_pixw, _pixh));
		}
	}

	if (!childmedia) {
		_parent->drawInfo(p, usex + usew, _height, usex * 2 + usew, selected, InfoDisplayOverBackground);

		if (via || reply) {
			int rectw = _width - usew - st::msgReplyPadding.left();
			int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
			if (via) {
				recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
			}
			if (reply) {
				recth += st::msgReplyBarSize.height();
			}
			int rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
			int recty = _height - recth;
			if (rtl()) rectx = _width - rectx - rectw;

			// Make the bottom of the rect at the same level as the bottom of the info rect.
			recty -= st::msgDateImgDelta;

			App::roundRect(p, rectx, recty, rectw, recth, selected ? st::msgServiceBgSelected : st::msgServiceBg, selected ? StickerSelectedCorners : StickerCorners);
			p.setPen(st::msgServiceFg);
			rectx += st::msgReplyPadding.left();
			rectw -= st::msgReplyPadding.left() + st::msgReplyPadding.right();
			if (via) {
				p.setFont(st::msgDateFont);
				p.drawTextLeft(rectx, recty + st::msgReplyPadding.top(), 2 * rectx + rectw, via->_text);
				int skip = st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
				recty += skip;
			}
			if (reply) {
				HistoryMessageReply::PaintFlags flags = 0;
				if (selected) {
					flags |= HistoryMessageReply::PaintSelected;
				}
				reply->paint(p, _parent, rectx, recty, rectw, flags);
			}
		}
	}
}

HistoryTextState HistorySticker::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;

	bool out = _parent->out(), isPost = _parent->isPost(), childmedia = (_parent->getMedia() != this);

	int usew = _maxw, usex = 0;
	auto via = childmedia ? nullptr : _parent->Get<HistoryMessageVia>();
	auto reply = childmedia ? nullptr : _parent->Get<HistoryMessageReply>();
	if (via || reply) {
		usew -= additionalWidth(via, reply);
		if (isPost) {
		} else if (out) {
			usex = _width - usew;
		}
	}
	if (rtl()) usex = _width - usex - usew;

	if (via || reply) {
		int rectw = _width - usew - st::msgReplyPadding.left();
		int recth = st::msgReplyPadding.top() + st::msgReplyPadding.bottom();
		if (via) {
			recth += st::msgServiceNameFont->height + (reply ? st::msgReplyPadding.top() : 0);
		}
		if (reply) {
			recth += st::msgReplyBarSize.height();
		}
		int rectx = isPost ? (usew + st::msgReplyPadding.left()) : (out ? 0 : (usew + st::msgReplyPadding.left()));
		int recty = _height - recth;
		if (rtl()) rectx = _width - rectx - rectw;

		// Make the bottom of the rect at the same level as the bottom of the info rect.
		recty -= st::msgDateImgDelta;

		if (via) {
			int viah = st::msgReplyPadding.top() + st::msgServiceNameFont->height + (reply ? 0 : st::msgReplyPadding.bottom());
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + viah) {
				result.link = via->_lnk;
				return result;
			}
			int skip = st::msgServiceNameFont->height + (reply ? 2 * st::msgReplyPadding.top() : 0);
			recty += skip;
			recth -= skip;
		}
		if (reply) {
			if (x >= rectx && y >= recty && x < rectx + rectw && y < recty + recth) {
				result.link = reply->replyToLink();
				return result;
			}
		}
	}
	if (_parent->getMedia() == this) {
		bool inDate = _parent->pointInTime(usex + usew, _height, x, y, InfoDisplayOverImage);
		if (inDate) {
			result.cursor = HistoryInDateCursorState;
		}
	}

	int pixLeft = usex + (usew - _pixw) / 2, pixTop = (_minh - _pixh) / 2;
	if (x >= pixLeft && x < pixLeft + _pixw && y >= pixTop && y < pixTop + _pixh) {
		result.link = _packLink;
		return result;
	}
	return result;
}

QString HistorySticker::toString() const {
	return _emoji.isEmpty() ? lang(lng_in_dlg_sticker) : lng_in_dlg_sticker_emoji(lt_emoji, _emoji);
}

QString HistorySticker::notificationText() const {
	return toString();
}

TextWithEntities HistorySticker::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + toString() + qsl(" ]"), EntitiesInText() };
}

void HistorySticker::attachToParent() {
	App::regDocumentItem(_data, _parent);
}

void HistorySticker::detachFromParent() {
	App::unregDocumentItem(_data, _parent);
}

void HistorySticker::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaDocument) {
		App::feedDocument(media.c_messageMediaDocument().vdocument, _data);
		if (!_data->data().isEmpty()) {
			Local::writeStickerImage(_data->mediaKey(), _data->data());
		}
	}
}

bool HistorySticker::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	return needReSetInlineResultDocument(media, _data);
}

ImagePtr HistorySticker::replyPreview() {
	return _data->makeReplyPreview();
}

int HistorySticker::additionalWidth(const HistoryMessageVia *via, const HistoryMessageReply *reply) const {
	int result = 0;
	if (via) {
		accumulate_max(result, st::msgReplyPadding.left() + st::msgReplyPadding.left() + via->_maxWidth + st::msgReplyPadding.left());
	}
	if (reply) {
		accumulate_max(result, st::msgReplyPadding.left() + reply->replyToWidth());
	}
	return result;
}

namespace {

ClickHandlerPtr sendMessageClickHandler(PeerData *peer) {
	return MakeShared<LambdaClickHandler>([peer] {
		Ui::showPeerHistory(peer->id, ShowAtUnreadMsgId, Ui::ShowWay::Forward);
	});
}

ClickHandlerPtr addContactClickHandler(HistoryItem *item) {
	return MakeShared<LambdaClickHandler>([fullId = item->fullId()] {
		if (auto item = App::histItemById(fullId)) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypeContact) {
					auto contact = static_cast<HistoryContact*>(media);
					auto fname = contact->fname();
					auto lname = contact->lname();
					auto phone = contact->phone();
					Ui::show(Box<AddContactBox>(fname, lname, phone));
				}
			}
		}
	});
}

} // namespace

HistoryContact::HistoryContact(HistoryItem *parent, int32 userId, const QString &first, const QString &last, const QString &phone) : HistoryMedia(parent)
, _userId(userId)
, _fname(first)
, _lname(last)
, _phone(App::formatPhone(phone)) {
	_name.setText(st::semiboldTextStyle, lng_full_name(lt_first_name, first, lt_last_name, last).trimmed(), _textNameOptions);
	_phonew = st::normalFont->width(_phone);
}

void HistoryContact::initDimensions() {
	_maxw = st::msgFileMinWidth;

	_contact = _userId ? App::userLoaded(_userId) : nullptr;
	if (_contact) {
		_contact->loadUserpic();
	} else {
		_photoEmpty.set(qAbs(_userId ? _userId : _parent->id) % kUserColorsCount, _name.originalText());
	}
	if (_contact && _contact->contact > 0) {
		_linkl = sendMessageClickHandler(_contact);
		_link = lang(lng_profile_send_message).toUpper();
	} else if (_userId) {
		_linkl = addContactClickHandler(_parent);
		_link = lang(lng_profile_add_contact).toUpper();
	}
	_linkw = _link.isEmpty() ? 0 : st::semiboldFont->width(_link);

	int32 tleft = 0, tright = 0;
	if (_userId) {
		tleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + tright);
	} else {
		tleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		tright = st::msgFileThumbPadding.left();
		_maxw = qMax(_maxw, tleft + _phonew + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	_maxw = qMax(tleft + _name.maxWidth() + tright, _maxw);
	_maxw = qMin(_maxw, int(st::msgMaxWidth));

	if (_userId) {
		_minh = st::msgFileThumbPadding.top() + st::msgFileThumbSize + st::msgFileThumbPadding.bottom();
		if (_parent->Has<HistoryMessageSigned>()) {
			_minh += st::msgDateFont->height - st::msgDateDelta.y();
		}
	} else {
		_minh = st::msgFilePadding.top() + st::msgFileSize + st::msgFilePadding.bottom();
	}
	if (!isBubbleTop()) {
		_minh -= st::msgFileTopMinus;
	}
	_height = _minh;
}

void HistoryContact::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	if (width >= _maxw) {
		width = _maxw;
	}

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		nametop = st::msgFileThumbNameTop - topMinus;
		nameright = st::msgFileThumbPadding.left();
		statustop = st::msgFileThumbStatusTop - topMinus;
		linktop = st::msgFileThumbLinkTop - topMinus;

		QRect rthumb(rtlrect(st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, st::msgFileThumbSize, st::msgFileThumbSize, width));
		if (_contact) {
			_contact->paintUserpic(p, rthumb.x(), rthumb.y(), st::msgFileThumbSize);
		} else {
			_photoEmpty.paint(p, st::msgFileThumbPadding.left(), st::msgFileThumbPadding.top() - topMinus, width, st::msgFileThumbSize);
		}
		if (selected) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(p.textPalette().selectOverlay);
			p.setPen(Qt::NoPen);
			p.drawEllipse(rthumb);
		}

		bool over = ClickHandler::showAsActive(_linkl);
		p.setFont(over ? st::semiboldFont->underline() : st::semiboldFont);
		p.setPen(outbg ? (selected ? st::msgFileThumbLinkOutFgSelected : st::msgFileThumbLinkOutFg) : (selected ? st::msgFileThumbLinkInFgSelected : st::msgFileThumbLinkInFg));
		p.drawTextLeft(nameleft, linktop, width, _link, _linkw);
	} else {
		nameleft = st::msgFilePadding.left() + st::msgFileSize + st::msgFilePadding.right();
		nametop = st::msgFileNameTop - topMinus;
		nameright = st::msgFilePadding.left();
		statustop = st::msgFileStatusTop - topMinus;

		_photoEmpty.paint(p, st::msgFilePadding.left(), st::msgFilePadding.top() - topMinus, width, st::msgFileSize);
	}
	int32 namewidth = width - nameleft - nameright;

	p.setFont(st::semiboldFont);
	p.setPen(outbg ? (selected ? st::historyFileNameOutFgSelected : st::historyFileNameOutFg) : (selected ? st::historyFileNameInFgSelected : st::historyFileNameInFg));
	_name.drawLeftElided(p, nameleft, nametop, namewidth, width);

	auto &status = outbg ? (selected ? st::mediaOutFgSelected : st::mediaOutFg) : (selected ? st::mediaInFgSelected : st::mediaInFg);
	p.setFont(st::normalFont);
	p.setPen(status);
	p.drawTextLeft(nameleft, statustop, width, _phone);
}

HistoryTextState HistoryContact::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0, linktop = 0;
	auto topMinus = isBubbleTop() ? 0 : st::msgFileTopMinus;
	if (_userId) {
		nameleft = st::msgFileThumbPadding.left() + st::msgFileThumbSize + st::msgFileThumbPadding.right();
		linktop = st::msgFileThumbLinkTop - topMinus;
		if (rtlrect(nameleft, linktop, _linkw, st::semiboldFont->height, _width).contains(x, y)) {
			result.link = _linkl;
			return result;
		}
	}
	if (x >= 0 && y >= 0 && x < _width && y < _height && _contact) {
		result.link = _contact->openLink();
		return result;
	}
	return result;
}

QString HistoryContact::notificationText() const {
	return lang(lng_in_dlg_contact);
}

TextWithEntities HistoryContact::selectedText(TextSelection selection) const {
	if (selection != FullSelection) {
		return TextWithEntities();
	}
	return { qsl("[ ") + lang(lng_in_dlg_contact) + qsl(" ]\n") + _name.originalText() + '\n' + _phone, EntitiesInText() };
}

void HistoryContact::attachToParent() {
	if (_userId) {
		App::regSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::detachFromParent() {
	if (_userId) {
		App::unregSharedContactItem(_userId, _parent);
	}
}

void HistoryContact::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaContact) {
		if (_userId != media.c_messageMediaContact().vuser_id.v) {
			detachFromParent();
			_userId = media.c_messageMediaContact().vuser_id.v;
			attachToParent();
		}
	}
}

namespace {

QString siteNameFromUrl(const QString &url) {
	QUrl u(url);
	QString pretty = u.isValid() ? u.toDisplayString() : url;
	QRegularExpressionMatch m = QRegularExpression(qsl("^[a-zA-Z0-9]+://")).match(pretty);
	if (m.hasMatch()) pretty = pretty.mid(m.capturedLength());
	int32 slash = pretty.indexOf('/');
	if (slash > 0) pretty = pretty.mid(0, slash);
	QStringList components = pretty.split('.', QString::SkipEmptyParts);
	if (components.size() >= 2) {
		components = components.mid(components.size() - 2);
		return components.at(0).at(0).toUpper() + components.at(0).mid(1) + '.' + components.at(1);
	}
	return QString();
}

int32 articleThumbWidth(PhotoData *thumb, int32 height) {
	int32 w = thumb->medium->width(), h = thumb->medium->height();
	return qMax(qMin(height * w / h, height), 1);
}

int32 articleThumbHeight(PhotoData *thumb, int32 width) {
	return qMax(thumb->medium->height() * width / thumb->medium->width(), 1);
}

int unitedLineHeight() {
	return qMax(st::webPageTitleFont->height, st::webPageDescriptionFont->height);
}

} // namespace

HistoryWebPage::HistoryWebPage(HistoryItem *parent, WebPageData *data) : HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
}

HistoryWebPage::HistoryWebPage(HistoryItem *parent, const HistoryWebPage &other) : HistoryMedia(parent)
, _data(other._data)
, _attach(other._attach ? other._attach->clone(parent) : nullptr)
, _asArticle(other._asArticle)
, _title(other._title)
, _description(other._description)
, _siteNameWidth(other._siteNameWidth)
, _durationWidth(other._durationWidth)
, _pixw(other._pixw)
, _pixh(other._pixh) {
}

void HistoryWebPage::initDimensions() {
	if (_data->pendingTill) {
		_maxw = _minh = _height = 0;
		return;
	}
	auto lineHeight = unitedLineHeight();

	if (!_openl && !_data->url.isEmpty()) {
		_openl = MakeShared<UrlClickHandler>(_data->url, true);
	}

	// init layout
	QString title(_data->title.isEmpty() ? _data->author : _data->title);
	if (!_data->description.isEmpty() && title.isEmpty() && _data->siteName.isEmpty() && !_data->url.isEmpty()) {
		_data->siteName = siteNameFromUrl(_data->url);
	}
	if (!_data->document && _data->photo && _data->type != WebPagePhoto && _data->type != WebPageVideo) {
		if (_data->type == WebPageProfile) {
			_asArticle = true;
		} else if (_data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
			_asArticle = false;
		} else {
			_asArticle = true;
		}
		if (_asArticle && _data->description.isEmpty() && title.isEmpty() && _data->siteName.isEmpty()) {
			_asArticle = false;
		}
	} else {
		_asArticle = false;
	}

	// init attach
	if (!_asArticle && !_attach) {
		if (_data->document) {
			if (_data->document->sticker()) {
				_attach = std::make_unique<HistorySticker>(_parent, _data->document);
			} else if (_data->document->isAnimation()) {
				_attach = std::make_unique<HistoryGif>(_parent, _data->document, QString());
			} else if (_data->document->isVideo()) {
				_attach = std::make_unique<HistoryVideo>(_parent, _data->document, QString());
			} else {
				_attach = std::make_unique<HistoryDocument>(_parent, _data->document, QString());
			}
		} else if (_data->photo) {
			_attach = std::make_unique<HistoryPhoto>(_parent, _data->photo, QString());
		}
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		auto text = _data->description;

		if (!_asArticle && !_attach) {
			text += _parent->skipBlock();
		}
		const TextParseOptions *opts = &_webpageDescriptionOptions;
		if (_data->siteName == qstr("Twitter")) {
			opts = &_twitterDescriptionOptions;
		} else if (_data->siteName == qstr("Instagram")) {
			opts = &_instagramDescriptionOptions;
		}
		_description.setText(st::webPageDescriptionStyle, text, *opts);
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		if (!_asArticle && !_attach && _description.isEmpty()) {
			title += _parent->skipBlock();
		}
		_title.setText(st::webPageTitleStyle, title, _webpageTitleOptions);
	}
	if (!_siteNameWidth && !_data->siteName.isEmpty()) {
		_siteNameWidth = st::webPageTitleFont->width(_data->siteName);
	}

	// init dimensions
	int32 l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	int32 skipBlockWidth = _parent->skipBlockWidth();
	_maxw = skipBlockWidth;
	_minh = 0;

	int32 siteNameHeight = _data->siteName.isEmpty() ? 0 : lineHeight;
	int32 titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	int32 descMaxLines = (3 + (siteNameHeight ? 0 : 1) + (titleMinHeight ? 0 : 1));
	int32 descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);
	int32 articleMinHeight = siteNameHeight + titleMinHeight + descriptionMinHeight;
	int32 articlePhotoMaxWidth = 0;
	if (_asArticle) {
		articlePhotoMaxWidth = st::webPagePhotoDelta + qMax(articleThumbWidth(_data->photo, articleMinHeight), lineHeight);
	}

	if (_siteNameWidth) {
		if (_title.isEmpty() && _description.isEmpty()) {
			accumulate_max(_maxw, _siteNameWidth + _parent->skipBlockWidth());
		} else {
			accumulate_max(_maxw, _siteNameWidth + articlePhotoMaxWidth);
		}
		_minh += lineHeight;
	}
	if (!_title.isEmpty()) {
		accumulate_max(_maxw, _title.maxWidth() + articlePhotoMaxWidth);
		_minh += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(_maxw, _description.maxWidth() + articlePhotoMaxWidth);
		_minh += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = !_siteNameWidth && _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) _minh += st::mediaInBubbleSkip;

		_attach->initDimensions();
		QMargins bubble(_attach->bubbleMargins());
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(_maxw, maxMediaWidth);
		_minh += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	if (_data->type == WebPageVideo && _data->duration) {
		_duration = formatDurationText(_data->duration);
		_durationWidth = st::msgDateFont->width(_duration);
	}
	_maxw += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	auto padding = inBubblePadding();
	_minh += padding.top() + padding.bottom();

	if (_asArticle) {
		_minh = resizeGetHeight(_maxw);
	}
}

int HistoryWebPage::resizeGetHeight(int width) {
	if (_data->pendingTill) {
		_width = width;
		_height = _minh;
		return _height;
	}

	_width = width/* = qMin(width, _maxw)*/;
	width -= st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();

	auto lineHeight = unitedLineHeight();
	auto linesMax = 5;
	auto siteNameLines = _siteNameWidth ? 1 : 0;
	auto siteNameHeight = _siteNameWidth ? lineHeight : 0;
	if (_asArticle) {
		_pixh = linesMax * lineHeight;
		do {
			_pixw = articleThumbWidth(_data->photo, _pixh);
			int32 wleft = width - st::webPagePhotoDelta - qMax(_pixw, int16(lineHeight));

			_height = siteNameHeight;

			if (_title.isEmpty()) {
				_titleLines = 0;
			} else {
				if (_title.countHeight(wleft) < 2 * st::webPageTitleFont->height) {
					_titleLines = 1;
				} else {
					_titleLines = 2;
				}
				_height += _titleLines * lineHeight;
			}

			auto descriptionHeight = _description.countHeight(wleft);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * lineHeight;

			if (_height >= _pixh) {
				break;
			}

			_pixh -= lineHeight;
		} while (_pixh > lineHeight);
		_height += bottomInfoPadding();
	} else {
		_height = siteNameHeight;

		if (_title.isEmpty()) {
			_titleLines = 0;
		} else {
			if (_title.countHeight(width) < 2 * st::webPageTitleFont->height) {
				_titleLines = 1;
			} else {
				_titleLines = 2;
			}
			_height += _titleLines * lineHeight;
		}

		if (_description.isEmpty()) {
			_descriptionLines = 0;
		} else {
			int32 descriptionHeight = _description.countHeight(width);
			if (descriptionHeight < (linesMax - siteNameLines - _titleLines) * st::webPageDescriptionFont->height) {
				_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
			} else {
				_descriptionLines = (linesMax - siteNameLines - _titleLines);
			}
			_height += _descriptionLines * lineHeight;
		}

		if (_attach) {
			auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
			if (!attachAtTop) _height += st::mediaInBubbleSkip;

			QMargins bubble(_attach->bubbleMargins());

			_attach->resizeGetHeight(width + bubble.left() + bubble.right());
			_height += _attach->height() - bubble.top() - bubble.bottom();
			if (isBubbleBottom() && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
				_height += bottomInfoPadding();
			}
		}
	}
	auto padding = inBubblePadding();
	_height += padding.top() + padding.bottom();

	return _height;
}

void HistoryWebPage::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	width -= padding.left() + padding.right();
	if (_asArticle || (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += bottomInfoPadding();
	}

	QRect bar(rtlrect(st::msgPadding.left(), tshift, st::webPageBar, _height - tshift - bshift, _width));
	p.fillRect(bar, barfg);

	auto lineHeight = unitedLineHeight();
	if (_asArticle) {
		_data->photo->medium->load(false, false);
		bool full = _data->photo->medium->loaded();
		QPixmap pix;
		int32 pw = qMax(_pixw, int16(lineHeight)), ph = _pixh;
		int32 pixw = _pixw, pixh = articleThumbHeight(_data->photo, _pixw);
		int32 maxw = convertScale(_data->photo->medium->width()), maxh = convertScale(_data->photo->medium->height());
		if (pixw * ph != pixh * pw) {
			float64 coef = (pixw * ph > pixh * pw) ? qMin(ph / float64(pixh), maxh / float64(pixh)) : qMin(pw / float64(pixw), maxw / float64(pixw));
			pixh = qRound(pixh * coef);
			pixw = qRound(pixw * coef);
		}
		if (full) {
			pix = _data->photo->medium->pixSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		} else {
			pix = _data->photo->thumb->pixBlurredSingle(pixw, pixh, pw, ph, ImageRoundRadius::Small);
		}
		p.drawPixmapLeft(padding.left() + width - pw, tshift, _width, pix);
		if (selected) {
			App::roundRect(p, rtlrect(padding.left() + width - pw, tshift, pw, _pixh, _width), p.textPalette().selectOverlay, SelectedOverlaySmallCorners);
		}
		width -= pw + st::webPagePhotoDelta;
	}
	if (_siteNameWidth) {
		p.setFont(st::webPageTitleFont);
		p.setPen(semibold);
		p.drawTextLeft(padding.left(), tshift, _width, (width >= _siteNameWidth) ? _data->siteName : st::webPageTitleFont->elided(_data->siteName, width));
		tshift += lineHeight;
	}
	if (_titleLines) {
		p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
		int32 endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, width, _width, _titleLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		int32 endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_description.drawLeftElided(p, padding.left(), tshift, width, _width, _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
		tshift += _descriptionLines * lineHeight;
	}
	if (_attach) {
		auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		p.translate(attachLeft, attachTop);

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		int32 pixwidth = _attach->currentWidth(), pixheight = _attach->height();

		if (_data->type == WebPageVideo && _attach->type() == MediaTypePhoto) {
			if (_attach->isReadyForOpen()) {
				if (_data->siteName == qstr("YouTube")) {
					st::youtubeIcon.paint(p, (pixwidth - st::youtubeIcon.width()) / 2, (pixheight - st::youtubeIcon.height()) / 2, _width);
				} else {
					st::videoIcon.paint(p, (pixwidth - st::videoIcon.width()) / 2, (pixheight - st::videoIcon.height()) / 2, _width);
				}
			}
			if (_durationWidth) {
				int32 dateX = pixwidth - _durationWidth - st::msgDateImgDelta - 2 * st::msgDateImgPadding.x();
				int32 dateY = pixheight - st::msgDateFont->height - 2 * st::msgDateImgPadding.y() - st::msgDateImgDelta;
				int32 dateW = pixwidth - dateX - st::msgDateImgDelta;
				int32 dateH = pixheight - dateY - st::msgDateImgDelta;

				App::roundRect(p, dateX, dateY, dateW, dateH, selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

				p.setFont(st::msgDateFont);
				p.setPen(st::msgDateImgFg);
				p.drawTextLeft(dateX + st::msgDateImgPadding.x(), dateY + st::msgDateImgPadding.y(), pixwidth, _duration);
			}
		}

		p.translate(-attachLeft, -attachTop);
	}
}

HistoryTextState HistoryWebPage::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (_asArticle || (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right())) {
		bshift += bottomInfoPadding();
	}
	width -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto inThumb = false;
	if (_asArticle) {
		int32 pw = qMax(_pixw, int16(lineHeight));
		if (rtlrect(padding.left() + width - pw, 0, pw, _pixh, _width).contains(x, y)) {
			inThumb = true;
		}
		width -= pw + st::webPagePhotoDelta;
	}
	int symbolAdd = 0;
	if (_siteNameWidth) {
		tshift += lineHeight;
	}
	if (_titleLines) {
		if (y >= tshift && y < tshift + _titleLines * lineHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = _title.getStateElidedLeft(x - padding.left(), y - tshift, width, _width, titleRequest);
		} else if (y >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		if (y >= tshift && y < tshift + _descriptionLines * lineHeight) {
			Text::StateRequestElided descriptionRequest = request.forText();
			descriptionRequest.lines = _descriptionLines;
			result = _description.getStateElidedLeft(x - padding.left(), y - tshift, width, _width, descriptionRequest);
		} else if (y >= tshift + _descriptionLines * lineHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionLines * lineHeight;
	}
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		auto attachAtTop = !_siteNameWidth && !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		if (x >= padding.left() && x < padding.left() + width && y >= tshift && y < _height - bshift) {
			auto attachLeft = padding.left() - bubble.left();
			auto attachTop = tshift - bubble.top();
			if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();
			result = _attach->getState(x - attachLeft, y - attachTop, request);

			if (result.link && !_data->document && _data->photo && _attach->isReadyForOpen()) {
				if (_data->type == WebPageProfile || _data->type == WebPageVideo) {
					result.link = _openl;
				} else if (_data->type == WebPagePhoto || _data->siteName == qstr("Twitter") || _data->siteName == qstr("Facebook")) {
					// leave photo link
				} else {
					result.link = _openl;
				}
			}
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryWebPage::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void HistoryWebPage::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryWebPage::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryWebPage::attachToParent() {
	App::regWebPageItem(_data, _parent);
	if (_attach) _attach->attachToParent();
}

void HistoryWebPage::detachFromParent() {
	App::unregWebPageItem(_data, _parent);
	if (_attach) _attach->detachFromParent();
}

TextWithEntities HistoryWebPage::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std::move(descriptionResult));
	return titleResult;
}

bool HistoryWebPage::hasReplyPreview() const {
	return _attach ? _attach->hasReplyPreview() : (_data->photo ? true : false);
}

ImagePtr HistoryWebPage::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

QMargins HistoryWebPage::inBubblePadding() const {
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int HistoryWebPage::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;

	// we use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

HistoryGame::HistoryGame(HistoryItem *parent, GameData *data) : HistoryMedia(parent)
, _data(data)
, _title(st::msgMinWidth - st::webPageLeft)
, _description(st::msgMinWidth - st::webPageLeft) {
}

HistoryGame::HistoryGame(HistoryItem *parent, const HistoryGame &other) : HistoryMedia(parent)
, _data(other._data)
, _attach(other._attach ? other._attach->clone(parent) : nullptr)
, _title(other._title)
, _description(other._description) {
}

void HistoryGame::initDimensions() {
	auto lineHeight = unitedLineHeight();

	if (!_openl) {
		_openl = MakeShared<ReplyMarkupClickHandler>(_parent, 0, 0);
	}

	auto title = _data->title;

	// init attach
	if (!_attach) {
		if (_data->document) {
			if (_data->document->sticker()) {
				_attach = std::make_unique<HistorySticker>(_parent, _data->document);
			} else if (_data->document->isAnimation()) {
				_attach = std::make_unique<HistoryGif>(_parent, _data->document, QString());
			} else if (_data->document->isVideo()) {
				_attach = std::make_unique<HistoryVideo>(_parent, _data->document, QString());
			} else {
				_attach = std::make_unique<HistoryDocument>(_parent, _data->document, QString());
			}
		} else if (_data->photo) {
			_attach = std::make_unique<HistoryPhoto>(_parent, _data->photo, QString());
		}
	}

	// init strings
	if (_description.isEmpty() && !_data->description.isEmpty()) {
		auto text = _data->description;
		if (!text.isEmpty()) {
			if (!_attach) {
				text += _parent->skipBlock();
			}
			_description.setText(st::webPageDescriptionStyle, text, _webpageDescriptionOptions);
		}
	}
	if (_title.isEmpty() && !title.isEmpty()) {
		_title.setText(st::webPageTitleStyle, title, _webpageTitleOptions);
	}

	// init dimensions
	int32 l = st::msgPadding.left() + st::webPageLeft, r = st::msgPadding.right();
	int32 skipBlockWidth = _parent->skipBlockWidth();
	_maxw = skipBlockWidth;
	_minh = 0;

	int32 titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	// enable any count of lines in game description / message
	int descMaxLines = 4096;
	int32 descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);

	if (!_title.isEmpty()) {
		accumulate_max(_maxw, _title.maxWidth());
		_minh += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(_maxw, _description.maxWidth());
		_minh += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) _minh += st::mediaInBubbleSkip;

		_attach->initDimensions();
		QMargins bubble(_attach->bubbleMargins());
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(_maxw, maxMediaWidth);
		_minh += _attach->minHeight() - bubble.top() - bubble.bottom();
	}
	_maxw += st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();
	auto padding = inBubblePadding();
	_minh += padding.top() + padding.bottom();

	if (!_gameTagWidth) {
		_gameTagWidth = st::msgDateFont->width(lang(lng_game_tag).toUpper());
	}
}

int HistoryGame::resizeGetHeight(int width) {
	_width = width = qMin(width, _maxw);
	width -= st::msgPadding.left() + st::webPageLeft + st::msgPadding.right();

	// enable any count of lines in game description / message
	auto linesMax = 4096;
	auto lineHeight = unitedLineHeight();
	_height = 0;
	if (_title.isEmpty()) {
		_titleLines = 0;
	} else {
		if (_title.countHeight(width) < 2 * st::webPageTitleFont->height) {
			_titleLines = 1;
		} else {
			_titleLines = 2;
		}
		_height += _titleLines * lineHeight;
	}

	if (_description.isEmpty()) {
		_descriptionLines = 0;
	} else {
		int32 descriptionHeight = _description.countHeight(width);
		if (descriptionHeight < (linesMax - _titleLines) * st::webPageDescriptionFont->height) {
			_descriptionLines = (descriptionHeight / st::webPageDescriptionFont->height);
		} else {
			_descriptionLines = (linesMax - _titleLines);
		}
		_height += _descriptionLines * lineHeight;
	}

	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) _height += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(width + bubble.left() + bubble.right());
		_height += _attach->height() - bubble.top() - bubble.bottom();
		if (isBubbleBottom() && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
			_height += bottomInfoPadding();
		}
	}
	auto padding = inBubblePadding();
	_height += padding.top() + padding.bottom();

	return _height;
}

void HistoryGame::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	width -= padding.left() + padding.right();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	QRect bar(rtlrect(st::msgPadding.left(), tshift, st::webPageBar, _height - tshift - bshift, _width));
	p.fillRect(bar, barfg);

	auto lineHeight = unitedLineHeight();
	if (_titleLines) {
		p.setPen(semibold);
		int32 endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, width, _width, _titleLines, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		int32 endskip = 0;
		if (_description.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_description.drawLeftElided(p, padding.left(), tshift, width, _width, _descriptionLines, style::al_left, 0, -1, endskip, false, toDescriptionSelection(selection));
		tshift += _descriptionLines * lineHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };

		p.translate(attachLeft, attachTop);
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->currentWidth();
		auto pixheight = _attach->height();

		auto gameW = _gameTagWidth + 2 * st::msgDateImgPadding.x();
		auto gameH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto gameX = pixwidth - st::msgDateImgDelta - gameW;
		auto gameY = pixheight - st::msgDateImgDelta - gameH;

		App::roundRect(p, rtlrect(gameX, gameY, gameW, gameH, pixwidth), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st::msgDateImgFg);
		p.drawTextLeft(gameX + st::msgDateImgPadding.x(), gameY + st::msgDateImgPadding.y(), pixwidth, lang(lng_game_tag).toUpper());

		p.translate(-attachLeft, -attachTop);
	}
}

HistoryTextState HistoryGame::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 width = _width, height = _height;

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}
	width -= padding.left() + padding.right();

	auto inThumb = false;
	auto symbolAdd = 0;
	auto lineHeight = unitedLineHeight();
	if (_titleLines) {
		if (y >= tshift && y < tshift + _titleLines * lineHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleLines;
			result = _title.getStateElidedLeft(x - padding.left(), y - tshift, width, _width, titleRequest);
		} else if (y >= tshift + _titleLines * lineHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleLines * lineHeight;
	}
	if (_descriptionLines) {
		if (y >= tshift && y < tshift + _descriptionLines * lineHeight) {
			Text::StateRequestElided descriptionRequest = request.forText();
			descriptionRequest.lines = _descriptionLines;
			result = _description.getStateElidedLeft(x - padding.left(), y - tshift, width, _width, descriptionRequest);
		} else if (y >= tshift + _descriptionLines * lineHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionLines * lineHeight;
	}
	if (inThumb) {
		result.link = _openl;
	} else if (_attach) {
		auto attachAtTop = !_titleLines && !_descriptionLines;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		if (x >= attachLeft && x < attachLeft + _attach->currentWidth() && y >= tshift && y < _height - bshift) {
			if (_attach->isReadyForOpen()) {
				result.link = _openl;
			} else {
				result = _attach->getState(x - attachLeft, y - attachTop, request);
			}
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryGame::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionLines || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

bool HistoryGame::consumeMessageText(const TextWithEntities &textWithEntities) {
	_description.setMarkedText(st::webPageDescriptionStyle, textWithEntities, itemTextOptions(_parent));
	return true;
}

void HistoryGame::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryGame::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryGame::attachToParent() {
	App::regGameItem(_data, _parent);
	if (_attach) _attach->attachToParent();
}

void HistoryGame::detachFromParent() {
	App::unregGameItem(_data, _parent);
	if (_attach) _attach->detachFromParent();
}

void HistoryGame::updateSentMedia(const MTPMessageMedia &media) {
	if (media.type() == mtpc_messageMediaGame) {
		auto &game = media.c_messageMediaGame().vgame;
		if (game.type() == mtpc_game) {
			App::feedGame(game.c_game(), _data);
		}
	}
}

bool HistoryGame::needReSetInlineResultMedia(const MTPMessageMedia &media) {
	updateSentMedia(media);
	return false;
}

QString HistoryGame::notificationText() const {
	QString result; // add a game controller emoji before game title
	result.reserve(_data->title.size() + 3);
	result.append(QChar(0xD83C)).append(QChar(0xDFAE)).append(QChar(' ')).append(_data->title);
	return result;
}

TextWithEntities HistoryGame::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std::move(descriptionResult));
	return titleResult;
}

ImagePtr HistoryGame::replyPreview() {
	return _attach ? _attach->replyPreview() : (_data->photo ? _data->photo->makeReplyPreview() : ImagePtr());
}

QMargins HistoryGame::inBubblePadding() const {
	auto lshift = st::msgPadding.left() + st::webPageLeft;
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.left() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int HistoryGame::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;

	// we use padding greater than st::msgPadding.bottom() in the
	// bottom of the bubble so that the left line looks pretty.
	// but if we have bottom skip because of the info display
	// we don't need that additional padding so we replace it
	// back with st::msgPadding.bottom() instead of left().
	result += st::msgPadding.bottom() - st::msgPadding.left();
	return result;
}

HistoryInvoice::HistoryInvoice(HistoryItem *parent, const MTPDmessageMediaInvoice &data) : HistoryMedia(parent)
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _status(st::msgMinWidth) {
	fillFromData(data);
}

HistoryInvoice::HistoryInvoice(HistoryItem *parent, const HistoryInvoice &other) : HistoryMedia(parent)
, _attach(other._attach ? other._attach->clone(parent) : nullptr)
, _titleHeight(other._titleHeight)
, _descriptionHeight(other._descriptionHeight)
, _title(other._title)
, _description(other._description)
, _status(other._status) {
}

QString HistoryInvoice::fillAmountAndCurrency(int amount, const QString &currency) {
	static auto shortCurrencyNames = QMap<QString, QString> {
		{ qsl("USD"), QString::fromUtf8("\x24") },
		{ qsl("GBP"), QString::fromUtf8("\xC2\xA3") },
		{ qsl("EUR"), QString::fromUtf8("\xE2\x82\xAC") },
		{ qsl("JPY"), QString::fromUtf8("\xC2\xA5") },
	};
	auto currencyText = shortCurrencyNames.value(currency, currency);
	return QLocale::system().toCurrencyString(amount / 100., currencyText);
	//auto amountBucks = amount / 100;
	//auto amountCents = amount % 100;
	//auto amountText = qsl("%1,%2").arg(amountBucks).arg(amountCents, 2, 10, QChar('0'));
	//return currencyText + amountText;
}

void HistoryInvoice::fillFromData(const MTPDmessageMediaInvoice &data) {
	// init attach
	if (data.has_photo() && data.vphoto.type() == mtpc_webDocument) {
		auto &doc = data.vphoto.c_webDocument();
		auto imageSize = QSize();
		for (auto &attribute : doc.vattributes.v) {
			if (attribute.type() == mtpc_documentAttributeImageSize) {
				auto &size = attribute.c_documentAttributeImageSize();
				imageSize = QSize(size.vw.v, size.vh.v);
				break;
			}
		}
		if (!imageSize.isEmpty()) {
			auto thumbsize = shrinkToKeepAspect(imageSize.width(), imageSize.height(), 100, 100);
			auto thumb = ImagePtr(thumbsize.width(), thumbsize.height());

			auto mediumsize = shrinkToKeepAspect(imageSize.width(), imageSize.height(), 320, 320);
			auto medium = ImagePtr(mediumsize.width(), mediumsize.height());

			// We don't use size from WebDocument, because it is not reliable.
			// It can be > 0 and different from the real size that we get in upload.WebFile result.
			auto filesize = 0; // doc.vsize.v;
			auto full = ImagePtr(WebFileImageLocation(imageSize.width(), imageSize.height(), doc.vdc_id.v, doc.vurl.v, doc.vaccess_hash.v), filesize);
			auto photoId = rand_value<PhotoId>();
			auto photo = App::photoSet(photoId, 0, 0, unixtime(), thumb, medium, full);

			_attach = std::make_unique<HistoryPhoto>(_parent, photo, QString());
		}
	}

	auto labelText = [&data] {
		if (data.has_receipt_msg_id()) {
			if (data.is_test()) {
				return lang(lng_payments_receipt_label_test);
			}
			return lang(lng_payments_receipt_label);
		} else if (data.is_test()) {
			return lang(lng_payments_invoice_label_test);
		}
		return lang(lng_payments_invoice_label);
	};
	auto statusText = TextWithEntities { fillAmountAndCurrency(data.vtotal_amount.v, qs(data.vcurrency)), EntitiesInText() };
	statusText.entities.push_back(EntityInText(EntityInTextBold, 0, statusText.text.size()));
	statusText.text += ' ' + labelText().toUpper();
	_status.setMarkedText(st::defaultTextStyle, statusText, itemTextOptions(_parent));

	_receiptMsgId = data.has_receipt_msg_id() ? data.vreceipt_msg_id.v : 0;

	// init strings
	auto description = qs(data.vdescription);
	if (!description.isEmpty()) {
		_description.setText(st::webPageDescriptionStyle, description, _webpageDescriptionOptions);
	}
	auto title = qs(data.vtitle);
	if (!title.isEmpty()) {
		_title.setText(st::webPageTitleStyle, title, _webpageTitleOptions);
	}
}

void HistoryInvoice::initDimensions() {
	auto lineHeight = unitedLineHeight();

	if (_attach) {
		if (_status.hasSkipBlock()) {
			_status.removeSkipBlock();
		}
	} else if (!_status.hasSkipBlock()) {
		_status.setSkipBlock(_parent->skipBlockWidth(), _parent->skipBlockHeight());
	}

	// init dimensions
	int32 l = st::msgPadding.left(), r = st::msgPadding.right();
	int32 skipBlockWidth = _parent->skipBlockWidth();
	_maxw = skipBlockWidth;
	_minh = 0;

	int32 titleMinHeight = _title.isEmpty() ? 0 : lineHeight;
	// enable any count of lines in game description / message
	int descMaxLines = 4096;
	int32 descriptionMinHeight = _description.isEmpty() ? 0 : qMin(_description.minHeight(), descMaxLines * lineHeight);

	if (!_title.isEmpty()) {
		accumulate_max(_maxw, _title.maxWidth());
		_minh += titleMinHeight;
	}
	if (!_description.isEmpty()) {
		accumulate_max(_maxw, _description.maxWidth());
		_minh += descriptionMinHeight;
	}
	if (_attach) {
		auto attachAtTop = _title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) _minh += st::mediaInBubbleSkip;

		_attach->initDimensions();
		auto bubble = _attach->bubbleMargins();
		auto maxMediaWidth = _attach->maxWidth() - bubble.left() - bubble.right();
		if (isBubbleBottom() && _attach->customInfoLayout()) {
			maxMediaWidth += skipBlockWidth;
		}
		accumulate_max(_maxw, maxMediaWidth);
		_minh += _attach->minHeight() - bubble.top() - bubble.bottom();
	} else {
		accumulate_max(_maxw, _status.maxWidth());
		_minh += st::mediaInBubbleSkip + _status.minHeight();
	}
	auto padding = inBubblePadding();
	_maxw += padding.left() + padding.right();
	_minh += padding.top() + padding.bottom();
}

int HistoryInvoice::resizeGetHeight(int width) {
	_width = width = qMin(width, _maxw);
	width -= st::msgPadding.left() + st::msgPadding.right();

	auto lineHeight = unitedLineHeight();

	_height = 0;
	if (_title.isEmpty()) {
		_titleHeight = 0;
	} else {
		if (_title.countHeight(width) < 2 * st::webPageTitleFont->height) {
			_titleHeight = lineHeight;
		} else {
			_titleHeight = 2 * lineHeight;
		}
		_height += _titleHeight;
	}

	if (_description.isEmpty()) {
		_descriptionHeight = 0;
	} else {
		_descriptionHeight = _description.countHeight(width);
		_height += _descriptionHeight;
	}

	if (_attach) {
		auto attachAtTop = !_title.isEmpty() && _description.isEmpty();
		if (!attachAtTop) _height += st::mediaInBubbleSkip;

		QMargins bubble(_attach->bubbleMargins());

		_attach->resizeGetHeight(width + bubble.left() + bubble.right());
		_height += _attach->height() - bubble.top() - bubble.bottom();
		if (isBubbleBottom() && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
			_height += bottomInfoPadding();
		}
	} else {
		_height += st::mediaInBubbleSkip + _status.countHeight(width);
	}
	auto padding = inBubblePadding();
	_height += padding.top() + padding.bottom();

	return _height;
}

void HistoryInvoice::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 width = _width, height = _height;

	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	auto &barfg = selected ? (outbg ? st::msgOutReplyBarSelColor : st::msgInReplyBarSelColor) : (outbg ? st::msgOutReplyBarColor : st::msgInReplyBarColor);
	auto &semibold = selected ? (outbg ? st::msgOutServiceFgSelected : st::msgInServiceFgSelected) : (outbg ? st::msgOutServiceFg : st::msgInServiceFg);
	auto &regular = selected ? (outbg ? st::msgOutDateFgSelected : st::msgInDateFgSelected) : (outbg ? st::msgOutDateFg : st::msgInDateFg);

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	width -= padding.left() + padding.right();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}

	auto lineHeight = unitedLineHeight();
	if (_titleHeight) {
		p.setPen(semibold);
		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outSemiboldPalette : st::inSemiboldPalette));

		int32 endskip = 0;
		if (_title.hasSkipBlock()) {
			endskip = _parent->skipBlockWidth();
		}
		_title.drawLeftElided(p, padding.left(), tshift, width, _width, _titleHeight / lineHeight, style::al_left, 0, -1, endskip, false, selection);
		tshift += _titleHeight;

		p.setTextPalette(selected ? (outbg ? st::outTextPaletteSelected : st::inTextPaletteSelected) : (outbg ? st::outTextPalette : st::inTextPalette));
	}
	if (_descriptionHeight) {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_description.drawLeft(p, padding.left(), tshift, width, _width, style::al_left, 0, -1, toDescriptionSelection(selection));
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		auto attachSelection = selected ? FullSelection : TextSelection { 0, 0 };

		p.translate(attachLeft, attachTop);
		_attach->draw(p, r.translated(-attachLeft, -attachTop), attachSelection, ms);
		auto pixwidth = _attach->currentWidth();
		auto pixheight = _attach->height();

		auto available = _status.maxWidth();
		auto statusW = available + 2 * st::msgDateImgPadding.x();
		auto statusH = st::msgDateFont->height + 2 * st::msgDateImgPadding.y();
		auto statusX = st::msgDateImgDelta;
		auto statusY = st::msgDateImgDelta;

		App::roundRect(p, rtlrect(statusX, statusY, statusW, statusH, pixwidth), selected ? st::msgDateImgBgSelected : st::msgDateImgBg, selected ? DateSelectedCorners : DateCorners);

		p.setFont(st::msgDateFont);
		p.setPen(st::msgDateImgFg);
		_status.drawLeftElided(p, statusX + st::msgDateImgPadding.x(), statusY + st::msgDateImgPadding.y(), available, pixwidth);

		p.translate(-attachLeft, -attachTop);
	} else {
		p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
		_status.drawLeft(p, padding.left(), tshift + st::mediaInBubbleSkip, width, _width);
	}
}

HistoryTextState HistoryInvoice::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 width = _width, height = _height;

	QMargins bubble(_attach ? _attach->bubbleMargins() : QMargins());
	auto padding = inBubblePadding();
	auto tshift = padding.top();
	auto bshift = padding.bottom();
	if (isBubbleBottom() && _attach && _attach->customInfoLayout() && _attach->currentWidth() + _parent->skipBlockWidth() > width + bubble.left() + bubble.right()) {
		bshift += bottomInfoPadding();
	}
	width -= padding.left() + padding.right();

	auto lineHeight = unitedLineHeight();
	auto symbolAdd = 0;
	if (_titleHeight) {
		if (y >= tshift && y < tshift + _titleHeight) {
			Text::StateRequestElided titleRequest = request.forText();
			titleRequest.lines = _titleHeight / lineHeight;
			result = _title.getStateElidedLeft(x - padding.left(), y - tshift, width, _width, titleRequest);
		} else if (y >= tshift + _titleHeight) {
			symbolAdd += _title.length();
		}
		tshift += _titleHeight;
	}
	if (_descriptionHeight) {
		if (y >= tshift && y < tshift + _descriptionHeight) {
			result = _description.getStateLeft(x - padding.left(), y - tshift, width, _width, request.forText());
		} else if (y >= tshift + _descriptionHeight) {
			symbolAdd += _description.length();
		}
		tshift += _descriptionHeight;
	}
	if (_attach) {
		auto attachAtTop = !_titleHeight && !_descriptionHeight;
		if (!attachAtTop) tshift += st::mediaInBubbleSkip;

		auto attachLeft = padding.left() - bubble.left();
		auto attachTop = tshift - bubble.top();
		if (rtl()) attachLeft = _width - attachLeft - _attach->currentWidth();

		if (x >= attachLeft && x < attachLeft + _attach->currentWidth() && y >= tshift && y < _height - bshift) {
			result = _attach->getState(x - attachLeft, y - attachTop, request);
		}
	}

	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryInvoice::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (!_descriptionHeight || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

void HistoryInvoice::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	if (_attach) {
		_attach->clickHandlerActiveChanged(p, active);
	}
}

void HistoryInvoice::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	if (_attach) {
		_attach->clickHandlerPressedChanged(p, pressed);
	}
}

void HistoryInvoice::attachToParent() {
	if (_attach) _attach->attachToParent();
}

void HistoryInvoice::detachFromParent() {
	if (_attach) _attach->detachFromParent();
}

QString HistoryInvoice::notificationText() const {
	return _title.originalText();
}

TextWithEntities HistoryInvoice::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		return TextWithEntities();
	}
	auto titleResult = _title.originalTextWithEntities(selection, ExpandLinksAll);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection), ExpandLinksAll);
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}

	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std::move(descriptionResult));
	return titleResult;
}

QMargins HistoryInvoice::inBubblePadding() const {
	auto lshift = st::msgPadding.left();
	auto rshift = st::msgPadding.right();
	auto bshift = isBubbleBottom() ? st::msgPadding.top() : st::mediaInBubbleSkip;
	auto tshift = isBubbleTop() ? st::msgPadding.bottom() : st::mediaInBubbleSkip;
	return QMargins(lshift, tshift, rshift, bshift);
}

int HistoryInvoice::bottomInfoPadding() const {
	if (!isBubbleBottom()) return 0;

	auto result = st::msgDateFont->height;
	return result;
}

HistoryLocation::HistoryLocation(HistoryItem *parent, const LocationCoords &coords, const QString &title, const QString &description) : HistoryMedia(parent)
, _data(App::location(coords))
, _title(st::msgMinWidth)
, _description(st::msgMinWidth)
, _link(new LocationClickHandler(coords)) {
	if (!title.isEmpty()) {
		_title.setText(st::webPageTitleStyle, textClean(title), _webpageTitleOptions);
	}
	if (!description.isEmpty()) {
		_description.setText(st::webPageDescriptionStyle, textClean(description), _webpageDescriptionOptions);
	}
}

HistoryLocation::HistoryLocation(HistoryItem *parent, const HistoryLocation &other) : HistoryMedia(parent)
, _data(other._data)
, _title(other._title)
, _description(other._description)
, _link(new LocationClickHandler(_data->coords)) {
}

void HistoryLocation::initDimensions() {
	bool bubble = _parent->hasBubble();

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_maxw = qMax(tw, int32(minWidth));
	_minh = qMax(th, int32(st::minPhotoSize));

	if (bubble) {
		_maxw += st::mediaPadding.left() + st::mediaPadding.right();
		if (!_title.isEmpty()) {
			_minh += qMin(_title.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			_minh += qMin(_description.countHeight(_maxw - st::msgPadding.left() - st::msgPadding.right()), 3 * st::webPageDescriptionFont->height);
		}
		_minh += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_minh += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				_minh += st::msgPadding.top();
			}
		}
	}
}

int HistoryLocation::resizeGetHeight(int width) {
	bool bubble = _parent->hasBubble();

	_width = qMin(width, _maxw);
	if (bubble) {
		_width -= st::mediaPadding.left() + st::mediaPadding.right();
	}

	int32 tw = fullWidth(), th = fullHeight();
	if (tw > st::maxMediaSize) {
		th = (st::maxMediaSize * th) / tw;
		tw = st::maxMediaSize;
	}
	_height = th;
	if (tw > _width) {
		_height = (_width * _height / tw);
	} else {
		_width = tw;
	}
	int32 minWidth = qMax(st::minPhotoSize, _parent->infoWidth() + 2 * (st::msgDateImgDelta + st::msgDateImgPadding.x()));
	_width = qMax(_width, int32(minWidth));
	_height = qMax(_height, int32(st::minPhotoSize));
	if (bubble) {
		_width += st::mediaPadding.left() + st::mediaPadding.right();
		_height += st::mediaPadding.top() + st::mediaPadding.bottom();
		if (!_title.isEmpty()) {
			_height += qMin(_title.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageTitleFont->height * 2);
		}
		if (!_description.isEmpty()) {
			_height += qMin(_description.countHeight(_width - st::msgPadding.left() - st::msgPadding.right()), st::webPageDescriptionFont->height * 3);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			_height += st::mediaInBubbleSkip;
			if (isBubbleTop()) {
				_height += st::msgPadding.top();
			}
		}
	}
	return _height;
}

void HistoryLocation::draw(Painter &p, const QRect &r, TextSelection selection, TimeMs ms) const {
	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();
	bool out = _parent->out(), isPost = _parent->isPost(), outbg = out && !isPost;
	bool selected = (selection == FullSelection);

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			p.setPen(outbg ? st::webPageTitleOutFg : st::webPageTitleInFg);
			_title.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 2, style::al_left, 0, -1, 0, false, selection);
			skipy += qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
		}
		if (!_description.isEmpty()) {
			p.setPen(outbg ? st::webPageDescriptionOutFg : st::webPageDescriptionInFg);
			_description.drawLeftElided(p, skipx + st::msgPadding.left(), skipy, textw, _width, 3, style::al_left, 0, -1, 0, false, toDescriptionSelection(selection));
			skipy += qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::mediaInBubbleSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	} else {
		App::roundShadow(p, 0, 0, width, height, selected ? st::msgInShadowSelected : st::msgInShadow, selected ? InSelectedShadowCorners : InShadowCorners);
	}

	_data->load();
	auto roundRadius = ImageRoundRadius::Large;
	auto roundCorners = ((isBubbleTop() && _title.isEmpty() && _description.isEmpty()) ? (ImageRoundCorner::TopLeft | ImageRoundCorner::TopRight) : ImageRoundCorner::None)
		| (isBubbleBottom() ? (ImageRoundCorner::BottomLeft | ImageRoundCorner::BottomRight) : ImageRoundCorner::None);
	auto rthumb = QRect(skipx, skipy, width, height);
	if (_data && !_data->thumb->isNull()) {
		int32 w = _data->thumb->width(), h = _data->thumb->height();
		QPixmap pix;
		if (width * h == height * w || (w == fullWidth() && h == fullHeight())) {
			pix = _data->thumb->pixSingle(width, height, width, height, roundRadius, roundCorners);
		} else if (width * h > height * w) {
			int32 nw = height * w / h;
			pix = _data->thumb->pixSingle(nw, height, width, height, roundRadius, roundCorners);
		} else {
			int32 nh = width * h / w;
			pix = _data->thumb->pixSingle(width, nh, width, height, roundRadius, roundCorners);
		}
		p.drawPixmap(rthumb.topLeft(), pix);
	} else {
		App::complexLocationRect(p, rthumb, roundRadius, roundCorners);
	}
	if (selected) {
		App::complexOverlayRect(p, rthumb, roundRadius, roundCorners);
	}

	if (_parent->getMedia() == this) {
		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		_parent->drawInfo(p, fullRight, fullBottom, skipx * 2 + width, selected, InfoDisplayOverImage);
	}
}

HistoryTextState HistoryLocation::getState(int x, int y, HistoryStateRequest request) const {
	HistoryTextState result;
	auto symbolAdd = 0;

	if (_width < st::msgPadding.left() + st::msgPadding.right() + 1) return result;
	int32 skipx = 0, skipy = 0, width = _width, height = _height;
	bool bubble = _parent->hasBubble();

	if (bubble) {
		skipx = st::mediaPadding.left();
		skipy = st::mediaPadding.top();

		if (!_title.isEmpty() || !_description.isEmpty()) {
			if (isBubbleTop()) {
				skipy += st::msgPadding.top();
			}
		}

		width -= st::mediaPadding.left() + st::mediaPadding.right();
		int32 textw = _width - st::msgPadding.left() - st::msgPadding.right();

		if (!_title.isEmpty()) {
			auto titleh = qMin(_title.countHeight(textw), 2 * st::webPageTitleFont->height);
			if (y >= skipy && y < skipy + titleh) {
				result = _title.getStateLeft(x - skipx - st::msgPadding.left(), y - skipy, textw, _width, request.forText());
				return result;
			} else if (y >= skipy + titleh) {
				symbolAdd += _title.length();
			}
			skipy += titleh;
		}
		if (!_description.isEmpty()) {
			auto descriptionh = qMin(_description.countHeight(textw), 3 * st::webPageDescriptionFont->height);
			if (y >= skipy && y < skipy + descriptionh) {
				result = _description.getStateLeft(x - skipx - st::msgPadding.left(), y - skipy, textw, _width, request.forText());
			} else if (y >= skipy + descriptionh) {
				symbolAdd += _description.length();
			}
			skipy += descriptionh;
		}
		if (!_title.isEmpty() || !_description.isEmpty()) {
			skipy += st::mediaInBubbleSkip;
		}
		height -= skipy + st::mediaPadding.bottom();
	}
	if (x >= skipx && y >= skipy && x < skipx + width && y < skipy + height && _data) {
		result.link = _link;

		int32 fullRight = skipx + width, fullBottom = _height - (skipx ? st::mediaPadding.bottom() : 0);
		bool inDate = _parent->pointInTime(fullRight, fullBottom, x, y, InfoDisplayOverImage);
		if (inDate) {
			result.cursor = HistoryInDateCursorState;
		}
	}
	result.symbol += symbolAdd;
	return result;
}

TextSelection HistoryLocation::adjustSelection(TextSelection selection, TextSelectType type) const {
	if (_description.isEmpty() || selection.to <= _title.length()) {
		return _title.adjustSelection(selection, type);
	}
	auto descriptionSelection = _description.adjustSelection(toDescriptionSelection(selection), type);
	if (selection.from >= _title.length()) {
		return fromDescriptionSelection(descriptionSelection);
	}
	auto titleSelection = _title.adjustSelection(selection, type);
	return { titleSelection.from, fromDescriptionSelection(descriptionSelection).to };
}

QString HistoryLocation::notificationText() const {
	return captionedNotificationText(lang(lng_maps_point), _title);
}

QString HistoryLocation::inDialogsText() const {
	return captionedInDialogsText(lang(lng_maps_point), _title);
}

TextWithEntities HistoryLocation::selectedText(TextSelection selection) const {
	if (selection == FullSelection) {
		TextWithEntities result = { qsl("[ ") + lang(lng_maps_point) + qsl(" ]\n"), EntitiesInText() };
		auto info = selectedText(AllTextSelection);
		if (!info.text.isEmpty()) {
			appendTextWithEntities(result, std::move(info));
			result.text.append('\n');
		}
		result.text += _link->dragText();
		return result;
	}

	auto titleResult = _title.originalTextWithEntities(selection);
	auto descriptionResult = _description.originalTextWithEntities(toDescriptionSelection(selection));
	if (titleResult.text.isEmpty()) {
		return descriptionResult;
	} else if (descriptionResult.text.isEmpty()) {
		return titleResult;
	}
	titleResult.text += '\n';
	appendTextWithEntities(titleResult, std::move(descriptionResult));
	return titleResult;
}

int32 HistoryLocation::fullWidth() const {
	return st::locationSize.width();
}

int32 HistoryLocation::fullHeight() const {
	return st::locationSize.height();
}
