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

using namespace SubtitleComposer;

// Private helper classes
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

// Piece compare function for QHash, used together with Piece::operator==()
inline uint
qHash(const VobSubInputProcessDialog::Piece &piece)
{
	// TODO: possibly implement better hashing algorithm
	return 1000 * piece.right * piece.bottom + piece.pixels.length();
}




// VobSubInputProcessDialog
VobSubInputProcessDialog::VobSubInputProcessDialog(Subtitle *subtitle, void *vob, void *spu, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::VobSubInputProcessDialog),
	m_vob(vob),
	m_spu(spu),
	m_subtitle(subtitle),
	m_subLastStartPts(0),
	m_subIndex(0),
	m_spaceWidth(-1),
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

	ui->lineEdit->installEventFilter(this);
	ui->lineEdit->setFocus();

	ui->progressBar->setMinimum(0);
	ui->progressBar->setValue(0);
	ui->progressBar->setMaximum(vobsub_get_packet_count(m_vob));

	processNextImage();
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
VobSubInputProcessDialog::processNextImage()
{
	void *packet;
	int timestamp;
	int len;

	while((len = vobsub_get_next_packet(m_vob, &packet, &timestamp)) > 0) {
		ui->progressBar->setValue(ui->progressBar->value() + 1);

		if(timestamp < 0)
			continue;

		spudec_assemble(m_spu, reinterpret_cast<unsigned char*>(packet), len, timestamp);
		spudec_heartbeat(m_spu, timestamp);

		unsigned char const *image;
		size_t image_size;
		unsigned width, height, stride, start_pts, end_pts;
		spudec_get_data(m_spu, &image, &image_size, &width, &height, &stride, &start_pts, &end_pts);

		// skip this packet if it is another packet of a subtitle that was decoded from multiple mpeg packets.
		if(start_pts == m_subLastStartPts)
			continue;
		m_subLastStartPts = start_pts;

		if(static_cast<unsigned>(timestamp) != start_pts)
			qWarning() << "VobSub Line " << m_subIndex << ": time stamp from .idx (" << timestamp << ") doesn't match time stamp from .sub (" << start_pts << ")\n";

		m_subShowTime.setMillisTime((double)start_pts / 90.);
		m_subHideTime.setMillisTime((double)end_pts / 90.);
		m_subText.clear();

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

		if(!m_subPixmap.loadFromData(pgmBuffer)) {
			qWarning() << "VobSub Line " << m_subIndex << ": Invalid pixmap - size: " << image_size << " bytes, " << width << "x" << height << " pixels\n";
			continue;
		}
		ui->subtitleView->setPixmap(m_subPixmap);

		m_subIndex++;

		if(processPieces()) {
			recognizePiece();
			return;
		}
	}

	accept();
}

bool
VobSubInputProcessDialog::processPieces()
{
	QImage pieceBitmap = m_subPixmap.toImage();
	int width = pieceBitmap.width();
	int height = pieceBitmap.height();
	int bgColor = pieceBitmap.pixel(0, 0);
	int color;
	PiecePtr piece;

	m_pieces.clear();

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
				m_pieces.append(piece);
			}
		}
	}

	if(m_pieces.empty())
		return false;

	// figure out where the lines are
	// TODO: fix accents of uppercase characters going into their own line, maybe merge
	//       very short (below average height?) lines with closest adjanced line
	QVector<LinePtr> lines;
	foreach(piece, m_pieces) {
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
	std::sort(m_pieces.begin(), m_pieces.end(), [](const PiecePtr &a, const PiecePtr &b)->bool{
		return *a < *b;
	});

	// TODO: improve whitespace calculation
	if(m_spaceWidth < 0) {
		m_spaceWidth = 0;
		foreach(piece, m_pieces)
			m_spaceWidth += piece->right - piece->left;
		m_spaceWidth /= 2 * m_pieces.size();
	}

	m_pieceCurrent = m_pieces.begin();
	return true;
}

void
VobSubInputProcessDialog::processCurrentPiece()
{
	if(m_pieceCurrent == m_pieces.end())
		return;

	QPixmap pixmap(m_subPixmap);
	QPainter p(&pixmap);
	p.setPen(Qt::red);

	QList<PiecePtr>::iterator i = m_pieceCurrent;
	int n = (*i)->symbolCount;
	while(n-- && i != m_pieces.end()) {
		foreach(QPoint pix, (*i)->pixels)
			p.drawPoint(pix);
		++i;
	}
	ui->subtitleView->setPixmap(pixmap);

	ui->lineEdit->setFocus();

	ui->symbolCount->setMaximum(m_pieces.end() - m_pieceCurrent);
}

void
VobSubInputProcessDialog::processNextPiece()
{
	auto piecePrev = m_pieceCurrent;
	m_pieceCurrent += (*m_pieceCurrent)->symbolCount;

	m_subText.append(currentText());

	ui->lineEdit->clear();
	ui->symbolCount->setValue(1);

	if(m_pieceCurrent == m_pieces.end()) {
		if(!m_subText.trimmed().isEmpty())
			m_subtitle->insertLine(new SubtitleLine(m_subText, m_subShowTime, m_subHideTime));

		processNextImage();
		return;
	}

	if(!(*piecePrev)->line->intersects((*m_pieceCurrent)->line))
		m_subText.append(QChar(QChar::LineFeed));
	else if((*m_pieceCurrent)->left - (*piecePrev)->right > m_spaceWidth)
		m_subText.append(QChar(QChar::Space));

	recognizePiece();
}

void
VobSubInputProcessDialog::recognizePiece()
{
	for(int len = m_recognizedPiecesMaxSymbolLength; len > 0; len--) {
		PiecePtr normal = currentNormalizedPiece(len);
		if(m_recognizedPieces.contains(*normal)) {
			const SString text = m_recognizedPieces.value(*normal);
			m_subText.append(text);
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

	PiecePtr normal = currentNormalizedPiece((*m_pieceCurrent)->symbolCount);
	m_recognizedPieces[*normal] = currentText();

	processNextPiece();
}

void
VobSubInputProcessDialog::onAbortClicked()
{
	reject();
}
