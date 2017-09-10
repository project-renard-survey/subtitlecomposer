/**
 * Copyright (C) 2007-2009 Sergio Pistone <sergio_pistone@yahoo.com.ar>
 * Copyright (C) 2010-2017 Mladen Milinkovic <max@smoothware.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "vobsubinputprocessdialog.h"
#include "ui_vobsubinputprocessdialog.h"

#include "mplayer/mp_msg.h"
#include "mplayer/vobsub.h"
#include "mplayer/spudec.h"

#include <QDebug>
#include <QPainter>
#include <QKeyEvent>

#include <QFile>
#include <QSaveFile>
#include <QDataStream>

#define VOBSUB_SYMBOL_FILE QStringLiteral("/home/max/killme-subtitlecomposer.dat")

using namespace SubtitleComposer;

// Private helper classes
class VobSubInputProcessDialog::Frame : public QSharedData
{
public:
	Frame() {}
	Frame(const Frame &other) : QSharedData(other) {}
	~Frame() {}

	bool processPieces();

	static unsigned spaceSum;
	static unsigned spaceCount;

	unsigned index;
	QPixmap subPixmap;
	Time subShowTime;
	Time subHideTime;
	QList<PiecePtr> pieces;
};

class VobSubInputProcessDialog::Piece : public QSharedData
{
public:
	Piece()
		: line(nullptr),
		  top(0),
		  left(0),
		  bottom(0),
		  right(0),
		  symbolCount(1) { }
	Piece(int x, int y)
		: line(nullptr),
		  top(y),
		  left(x),
		  bottom(y),
		  right(x),
		  symbolCount(1) { }
	Piece(const Piece &other)
		: QSharedData(other),
		  line(other.line),
		  top(other.top),
		  left(other.left),
		  bottom(other.bottom),
		  right(other.right),
		  symbolCount(other.symbolCount),
		  pixels(other.pixels) { }
	~Piece() { }

	inline int height() {
		return bottom - top + 1;
	}

	inline bool operator<(const Piece &other) const;
	inline bool operator==(const Piece &other) const;
	inline Piece & operator+=(const Piece &other);


	inline void normalize();

	LinePtr line;
	int top, left, bottom, right;
	int symbolCount;
	SString text;
	QVector<QPoint> pixels;
};

class VobSubInputProcessDialog::Line : public QSharedData {
public:
	Line(int top, int bottom)
		: top(top),
		  bottom(bottom) { }
	Line(const Line &other)
		: QSharedData(other),
		  top(other.top),
		  bottom(other.bottom) { }

	inline bool contains(PiecePtr piece) {
		return top <= piece->bottom && piece->top <= bottom;
	}
	inline bool intersects(LinePtr line) {
		return top <= line->bottom && line->top <= bottom;
	}
	inline void extend(int top, int bottom) {
		if(top < this->top)
			this->top = top;
		if(bottom > this->bottom)
			this->bottom = bottom;
	}

	int top, bottom;
};

unsigned VobSubInputProcessDialog::Frame::spaceSum;
unsigned VobSubInputProcessDialog::Frame::spaceCount;

bool
VobSubInputProcessDialog::Frame::processPieces()
{
	QImage pieceBitmap = subPixmap.toImage();
	int width = pieceBitmap.width();
	int height = pieceBitmap.height();
	int bgColor = pieceBitmap.pixel(0, 0);
	int color;
	PiecePtr piece;

	pieces.clear();

	// build piece by searching non-diagonal adjanced pixels, assigned pixels are
	// removed from pieceBitmap
	std::function<void(int,int)> cutPiece = [&](int x, int y){
		if(piece->top > y)
			piece->top = y;
		if(piece->bottom < y)
			piece->bottom = y;

		if(piece->left > x)
			piece->left = x;
		if(piece->right < x)
			piece->right = x;

		piece->pixels.append(QPoint(x, y));
		pieceBitmap.setPixel(x, y, bgColor);

		if(x < width - 1 && qGray(pieceBitmap.pixel(x + 1, y)) > 127)
			cutPiece(x + 1, y);
		if(x > 0 && qGray(pieceBitmap.pixel(x - 1, y)) > 127)
			cutPiece(x - 1, y);
		if(y < height - 1 && qGray(pieceBitmap.pixel(x, y + 1)) > 127)
			cutPiece(x, y + 1);
		if(y > 0 && qGray(pieceBitmap.pixel(x, y - 1)) > 127)
			cutPiece(x, y - 1);
	};

	// search pieces from top left
	for(int y = 0; y < height; y++) {
		for(int x = 0; x < width; x++) {
			color = qGray(pieceBitmap.pixel(x, y));
			if(color > 127) {
				piece = new Piece(x, y);
				cutPiece(x, y);
				pieces.append(piece);
			}
		}
	}

	if(pieces.empty())
		return false;

	// figure out where the lines are
	// TODO: fix accents of uppercase characters going into their own line, maybe merge
	//       very short (below average height?) lines with closest adjanced line
	QVector<LinePtr> lines;
	foreach(piece, pieces) {
		foreach(auto line, lines) {
			if(line->contains(piece)) {
				piece->line = line;
				line->extend(piece->top, piece->bottom);
				break;
			}
		}
		if(!piece->line) {
			piece->line = new Line(piece->top, piece->bottom);
			lines.append(piece->line);
		}
	}

	// sort pieces, line by line, left to right, comparison is done in Piece::operator<()
	std::sort(pieces.begin(), pieces.end(), [](const PiecePtr &a, const PiecePtr &b)->bool{
		return *a < *b;
	});

	foreach(piece, pieces) {
		spaceSum += piece->right - piece->left;
		spaceCount++;
	}

	return true;
}

inline bool
VobSubInputProcessDialog::Piece::operator<(const Piece &other) const
{
	if(line->top < other.line->top)
		return true;
	if(line->intersects(other.line) && left < other.left)
		return true;
	return false;
}

inline bool
VobSubInputProcessDialog::Piece::operator==(const Piece &other) const
{
	if(bottom - top != other.bottom - other.top)
		return false;
	if(right - left != other.right - other.left)
		return false;
	if(symbolCount != other.symbolCount)
		return false;
	// we assume pixel vectors contain QPoint elements ordered exactly the same
	return pixels == other.pixels;
}

inline VobSubInputProcessDialog::Piece &
VobSubInputProcessDialog::Piece::operator+=(const Piece &other)
{
	if(top > other.top)
		top = other.top;
	if(bottom < other.bottom)
		bottom = other.bottom;

	if(left > other.left)
		left = other.left;
	if(right < other.right)
		right = other.right;

	pixels.append(other.pixels);

	return *this;
}

// write to QDataStream
inline QDataStream &
operator<<(QDataStream &stream, const SubtitleComposer::VobSubInputProcessDialog::Line &line) {
	stream << line.top << line.bottom;
	return stream;
}

inline QDataStream &
operator<<(QDataStream &stream, const SubtitleComposer::VobSubInputProcessDialog::Piece &piece) {
	stream << *piece.line;
	stream << piece.top << piece.left << piece.bottom << piece.right;
	stream << piece.symbolCount;
	stream << piece.text;
	stream << piece.pixels;
	return stream;
}

// read from QDataStream
inline QDataStream &
operator>>(QDataStream &stream, SubtitleComposer::VobSubInputProcessDialog::Line &line) {
	stream >> line.top >> line.bottom;
	return stream;
}

inline QDataStream &
operator>>(QDataStream &stream, SubtitleComposer::VobSubInputProcessDialog::Piece &piece) {
	piece.line = new VobSubInputProcessDialog::Line(0, 0);
	stream >> *piece.line;
	stream >> piece.top >> piece.left >> piece.bottom >> piece.right;
	stream >> piece.symbolCount;
	stream >> piece.text;
	stream >> piece.pixels;
	return stream;
}

inline void
VobSubInputProcessDialog::Piece::normalize()
{
	if(top == 0 && left == 0)
		return;

	for(auto i = pixels.begin(); i != pixels.end(); ++i) {
		i->rx() -= left;
		i->ry() -= top;
	}

	right -= left;
	bottom -= top;
	top = left = 0;
}

inline uint
qHash(const VobSubInputProcessDialog::Piece &piece)
{
	// ignore top and left since this is used on normalized pieces
	return 1000 * piece.right * piece.bottom + piece.pixels.length();
}




// VobSubInputProcessDialog
VobSubInputProcessDialog::VobSubInputProcessDialog(Subtitle *subtitle, void *vob, void *spu, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::VobSubInputProcessDialog),
	m_subtitle(subtitle),
	m_recognizedPiecesMaxSymbolLength(0)
{
	ui->setupUi(this);

	connect(ui->btnOk, &QPushButton::clicked, this, &VobSubInputProcessDialog::onOkClicked);
	connect(ui->btnAbort, &QPushButton::clicked, this, &VobSubInputProcessDialog::onAbortClicked);

	connect(ui->styleBold, &QPushButton::toggled, [this](bool checked){
		QFont font = ui->lineEdit->font();
		font.setBold(checked);
		ui->lineEdit->setFont(font);
	});
	connect(ui->styleItalic, &QPushButton::toggled, [this](bool checked){
		QFont font = ui->lineEdit->font();
		font.setItalic(checked);
		ui->lineEdit->setFont(font);
	});
	connect(ui->styleUnderline, &QPushButton::toggled, [this](bool checked){
		QFont font = ui->lineEdit->font();
		font.setUnderline(checked);
		ui->lineEdit->setFont(font);
	});

	connect(ui->symbolCount, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &VobSubInputProcessDialog::onSymbolCountChanged);

	connect(ui->btnPrevSymbol, &QPushButton::clicked, this, &VobSubInputProcessDialog::onPrevSymbolClicked);
	connect(ui->btnPrevImage, &QPushButton::clicked, this, &VobSubInputProcessDialog::onPrevImageClicked);

	ui->lineEdit->installEventFilter(this);
	ui->lineEdit->setFocus();

	Frame::spaceSum = Frame::spaceCount = 0;

	ui->progressBar->setMinimum(0);
	ui->progressBar->setValue(0);
	processFrames(vob, spu);
	ui->progressBar->setMaximum(m_frames.size());

	m_spaceWidth = Frame::spaceSum / Frame::spaceCount;

#ifdef VOBSUB_SYMBOL_FILE
	QFile file(VOBSUB_SYMBOL_FILE);
	if(file.open(QIODevice::ReadOnly)) {
		QDataStream stream(&file);
		stream >> m_recognizedPieces;
		stream >> m_recognizedPiecesMaxSymbolLength;
		file.close();
	}

	connect(this, &QDialog::finished, [this](int){
		QSaveFile file(VOBSUB_SYMBOL_FILE);
		if(file.open(QIODevice::WriteOnly)) {
			QDataStream stream(&file);
			stream << m_recognizedPieces;
			stream << m_recognizedPiecesMaxSymbolLength;
			file.commit();
		}
	});
#endif

	ui->grpText->setDisabled(true);
	ui->btnPrevSymbol->setDisabled(true);
	ui->btnPrevImage->setDisabled(true);
	QMetaObject::invokeMethod(this, "processNextImage", Qt::QueuedConnection);
//	processNextImage();
}

VobSubInputProcessDialog::~VobSubInputProcessDialog()
{
	delete ui;
}


/*virtual*/ bool
VobSubInputProcessDialog::eventFilter(QObject *obj, QEvent *event) /*override*/
{
	if(event->type() == QEvent::FocusOut) {
		ui->lineEdit->setFocus();
		return true;
	}

	if(event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
		switch(keyEvent->key()) {
		case Qt::Key_Up:
			ui->symbolCount->setValue(ui->symbolCount->value() + ui->symbolCount->singleStep());
			return true;

		case Qt::Key_Down:
			ui->symbolCount->setValue(ui->symbolCount->value() - ui->symbolCount->singleStep());
			return true;

		case Qt::Key_Space:
		case Qt::Key_Escape:
			return true;

		case Qt::Key_B:
			if((keyEvent->modifiers() & Qt::ControlModifier) == 0)
				break;
			ui->styleBold->toggle();
			return true;

		case Qt::Key_I:
			if((keyEvent->modifiers() & Qt::ControlModifier) == 0)
				break;
			ui->styleItalic->toggle();
			return true;

		case Qt::Key_U:
			if((keyEvent->modifiers() & Qt::ControlModifier) == 0)
				break;
			ui->styleUnderline->toggle();
			return true;
		}
	}

	return QDialog::eventFilter(obj, event);
}

void
VobSubInputProcessDialog::processFrames(void *vob, void *spu)
{
	void *packet;
	int timestamp;
	int len;
	unsigned lastStartPts = 0;
	FramePtr frame;
	unsigned index = 0;

	while((len = vobsub_get_next_packet(vob, &packet, &timestamp)) > 0) {
		if(timestamp < 0)
			continue;

		frame = new Frame();
		frame->index = index;

		spudec_assemble(spu, reinterpret_cast<unsigned char*>(packet), len, timestamp);
		spudec_heartbeat(spu, timestamp);

		unsigned char const *image;
		size_t image_size;
		unsigned width, height, stride, start_pts, end_pts;
		spudec_get_data(spu, &image, &image_size, &width, &height, &stride, &start_pts, &end_pts);

		// skip this packet if it is another packet of a subtitle that was decoded from multiple mpeg packets.
		if(start_pts == lastStartPts)
			continue;
		lastStartPts = start_pts;

		if(static_cast<unsigned>(timestamp) != start_pts)
			qWarning() << "VobSub Line " << index << ": time stamp from .idx (" << timestamp << ") doesn't match time stamp from .sub (" << start_pts << ")\n";

		frame->subShowTime.setMillisTime((double)start_pts / 90.);
		frame->subHideTime.setMillisTime((double)end_pts / 90.);

		QByteArray pgmBuffer;
		pgmBuffer
				.append("P5\n")
				.append(QByteArray::number(width))
				.append(" ")
				.append(QByteArray::number(height))
				.append(" ")
				.append(QByteArray::number(255))
				.append("\n")
				.append(reinterpret_cast<const char *>(image), image_size);

		if(!frame->subPixmap.loadFromData(pgmBuffer)) {
			qWarning() << "VobSub Line " << index << ": Invalid pixmap - size: " << image_size << " bytes, " << width << "x" << height << " pixels\n";
			continue;
		}

		if(frame->processPieces()) {
			m_frames.append(frame);
			index++;
		}
	}

	m_frameCurrent = m_frames.begin() - 1;
}

void
VobSubInputProcessDialog::processNextImage()
{
	if(++m_frameCurrent == m_frames.end()) {
		accept();
		return;
	}

	ui->progressBar->setValue((*m_frameCurrent)->index + 1);

	QRect rcVisible((*m_frameCurrent)->subPixmap.rect());
	rcVisible.setWidth(300);
	rcVisible.setHeight(100);
	ui->subtitleView->setPixmap((*m_frameCurrent)->subPixmap.copy(rcVisible));

	m_pieces = (*m_frameCurrent)->pieces;
	m_pieceCurrent = m_pieces.begin();

	recognizePiece();
}

void
VobSubInputProcessDialog::processCurrentPiece()
{
	if(m_pieceCurrent == m_pieces.end())
		return;

	ui->grpText->setDisabled(false);
	ui->btnPrevSymbol->setDisabled(false);
	ui->btnPrevImage->setDisabled(false);

	QPixmap pixmap((*m_frameCurrent)->subPixmap);
	QPainter p(&pixmap);
	p.setPen(Qt::red);

	QList<PiecePtr>::iterator i = m_pieceCurrent;
	QRect rcVisible(QPoint((*i)->left, (*i)->top), QPoint((*i)->right, (*i)->bottom));
	int n = (*i)->symbolCount;
	while(n-- && i != m_pieces.end()) {
		rcVisible |= QRect(QPoint((*i)->left, (*i)->top), QPoint((*i)->right, (*i)->bottom));
		foreach(QPoint pix, (*i)->pixels)
			p.drawPoint(pix);
		++i;
	}
	int w = (300 - rcVisible.width()) / 2;
	int h = (100 - rcVisible.height()) / 2;
	rcVisible.adjust(-w, -h, w, h);
	ui->subtitleView->setPixmap(pixmap.copy(rcVisible));

	ui->lineEdit->setFocus();

	ui->symbolCount->setMaximum(m_pieces.end() - m_pieceCurrent);
}

void
VobSubInputProcessDialog::processNextPiece()
{
	m_pieceCurrent += (*m_pieceCurrent)->symbolCount;

	ui->lineEdit->clear();
	ui->symbolCount->setValue(1);

	if(m_pieceCurrent == m_pieces.end()) {
		SString subText;
		PiecePtr piecePrev;
		foreach(PiecePtr piece, m_pieces) {
			if(piecePrev) {
				if(!piecePrev->line->intersects(piece->line))
					subText.append(QChar(QChar::LineFeed));
				else if(piece->left - piecePrev->right > m_spaceWidth)
					subText.append(QChar(QChar::Space));
			}

			subText += piece->text;
			piecePrev = piece;
		}

		m_subtitle->insertLine(new SubtitleLine(subText, (*m_frameCurrent)->subShowTime, (*m_frameCurrent)->subHideTime));

		ui->grpText->setDisabled(true);
		ui->btnPrevSymbol->setDisabled(true);
		ui->btnPrevImage->setDisabled(true);
		QMetaObject::invokeMethod(this, "processNextImage", Qt::QueuedConnection);
//		processNextImage();
		return;
	}

	recognizePiece();
}

void
VobSubInputProcessDialog::recognizePiece()
{
	for(int len = m_recognizedPiecesMaxSymbolLength; len > 0; len--) {
		PiecePtr normal = currentNormalizedPiece(len);
		if(m_recognizedPieces.contains(*normal)) {
			const SString text = m_recognizedPieces.value(*normal);
			(*m_pieceCurrent)->text = text;
			currentSymbolCountSet(len);
			processNextPiece();
			return;
		}
	}

	processCurrentPiece();
}

SString
VobSubInputProcessDialog::currentText()
{
	int style = 0;
	if(ui->styleBold->isChecked())
		style |= SString::Bold;
	if(ui->styleItalic->isChecked())
		style |= SString::Italic;
	if(ui->styleUnderline->isChecked())
		style |= SString::Underline;
	return SString(ui->lineEdit->text(), style);
}

VobSubInputProcessDialog::PiecePtr
VobSubInputProcessDialog::currentNormalizedPiece(int symbolCount)
{
	PiecePtr normal(new Piece(**m_pieceCurrent));
	normal->symbolCount = symbolCount;

	for(auto piece = m_pieceCurrent; --symbolCount && ++piece != m_pieces.end(); )
		*normal += **piece;

	normal->normalize();

	return normal;
}

void
VobSubInputProcessDialog::currentSymbolCountSet(int symbolCount)
{
	if(m_pieceCurrent == m_pieces.end())
		return;

	int n = (*m_pieceCurrent)->symbolCount;
	QList<PiecePtr>::iterator piece = m_pieceCurrent;
	while(--n && ++piece != m_pieces.end())
		(*piece)->symbolCount = 1;

	piece = m_pieceCurrent;
	(*piece)->symbolCount = symbolCount;
	while(--symbolCount && ++piece != m_pieces.end())
		(*piece)->symbolCount = 0;
}

void
VobSubInputProcessDialog::onSymbolCountChanged(int symbolCount)
{
	currentSymbolCountSet(symbolCount);

	processCurrentPiece();
}

void
VobSubInputProcessDialog::onOkClicked()
{
	if((*m_pieceCurrent)->symbolCount > m_recognizedPiecesMaxSymbolLength)
		m_recognizedPiecesMaxSymbolLength = (*m_pieceCurrent)->symbolCount;

	(*m_pieceCurrent)->text = currentText();

	PiecePtr normal = currentNormalizedPiece((*m_pieceCurrent)->symbolCount);
	m_recognizedPieces[*normal] = (*m_pieceCurrent)->text;

	processNextPiece();
}

void
VobSubInputProcessDialog::onAbortClicked()
{
	reject();
}

void
VobSubInputProcessDialog::onPrevImageClicked()
{
	if(m_frameCurrent == m_frames.begin())
		return;

	--m_frameCurrent;
	m_subtitle->removeLines(RangeList(Range(m_subtitle->lastIndex())), Subtitle::Both);

	ui->progressBar->setValue((*m_frameCurrent)->index + 1);

	m_pieces = (*m_frameCurrent)->pieces;
	m_pieceCurrent = m_pieces.end();

	onPrevSymbolClicked();
}

void
VobSubInputProcessDialog::onPrevSymbolClicked()
{
	do {
		if(m_pieceCurrent == m_pieces.begin())
			return onPrevImageClicked();
		--m_pieceCurrent;
	} while((*m_pieceCurrent)->symbolCount == 0);

	processCurrentPiece();

	ui->lineEdit->setText((*m_pieceCurrent)->text.string());
	ui->lineEdit->selectAll();

	int style = (*m_pieceCurrent)->text.styleFlagsAt(0);
	ui->styleBold->setChecked((style & SString::Bold) != 0);
	ui->styleItalic->setChecked((style & SString::Italic) != 0);
	ui->styleUnderline->setChecked((style & SString::Underline) != 0);

	ui->symbolCount->setValue((*m_pieceCurrent)->symbolCount);
}
