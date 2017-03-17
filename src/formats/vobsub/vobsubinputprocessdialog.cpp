#include "vobsubinputprocessdialog.h"
#include "ui_vobsubinputprocessdialog.h"

#include "mplayer/mp_msg.h"
#include "mplayer/vobsub.h"
#include "mplayer/spudec.h"

#include <QDebug>
#include <QPushButton>


using namespace SubtitleComposer;

VobSubInputProcessDialog::VobSubInputProcessDialog(Subtitle *subtitle, void *vob, void *spu, QWidget *parent) :
	QDialog(parent),
	ui(new Ui::VobSubInputProcessDialog),
	m_vob(vob),
	m_spu(spu),
	m_subtitle(subtitle),
	m_subLastStartPts(0),
	m_subIndex(0)
{
	ui->setupUi(this);

	connect(ui->buttonBox->button(QDialogButtonBox::Apply), SIGNAL(clicked()), SLOT(onApplyClicked()));
	connect(ui->buttonBox->button(QDialogButtonBox::Discard), SIGNAL(clicked()), SLOT(onDiscardClicked()));

	processNextImage();
}

VobSubInputProcessDialog::~VobSubInputProcessDialog()
{
	delete ui;
}

void
VobSubInputProcessDialog::processNextImage()
{
	void *packet;
	int timestamp;
	int len;
	while((len = vobsub_get_next_packet(m_vob, &packet, &timestamp)) > 0) {
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
		break;
	}
}

void
VobSubInputProcessDialog::onApplyClicked()
{
	m_subtitle->insertLine(new SubtitleLine(m_subText, m_subShowTime, m_subHideTime));

	processNextImage();
}

void
VobSubInputProcessDialog::onDiscardClicked()
{
	processNextImage();
}
