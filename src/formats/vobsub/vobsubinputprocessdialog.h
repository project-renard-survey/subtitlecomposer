#ifndef VOBSUBINPUTPROCESSDIALOG_H
#define VOBSUBINPUTPROCESSDIALOG_H

#include "core/subtitle.h"

#include <QDialog>

namespace Ui {
class VobSubInputProcessDialog;
}

namespace SubtitleComposer {
class VobSubInputProcessDialog : public QDialog
{
	Q_OBJECT

public:
	VobSubInputProcessDialog(Subtitle *subtitle, void *vob, void *spu, QWidget *parent = 0);
	~VobSubInputProcessDialog();

private slots:
	void onApplyClicked();
	void onDiscardClicked();

private:
	Ui::VobSubInputProcessDialog *ui;

	void processNextImage();

	void *m_vob;
	void *m_spu;

	Subtitle *m_subtitle;
	QPixmap m_subPixmap;
	SString m_subText;
	Time m_subShowTime;
	Time m_subHideTime;
	unsigned m_subLastStartPts;
	unsigned m_subIndex;
};
}

#endif // VOBSUBINPUTPROCESSDIALOG_H
