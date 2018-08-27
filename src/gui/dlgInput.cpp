/*
 *  Copyright (C) 2010-2017 Fabio Cavallo (aka FHorse)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#if defined (__linux__)
#include <unistd.h>
#include <fcntl.h>
#endif
#include "dlgInput.moc"
#include "mainWindow.hpp"
#include "dlgStdPad.hpp"
#include "emu.h"
#if defined (WITH_OPENGL)
#include "opengl.h"
#endif
#include "gui.h"

typedef struct _cb_ports {
	QString desc;
	int value;
} _cb_ports;

enum dlg_input_shcut_mode { UPDATE_ALL, BUTTON_PRESSED, NO_ACTION = 255 };

dlgInput::dlgInput(QWidget *parent = 0) : QDialog(parent) {
	memset(&data, 0x00, sizeof(data));
	memcpy(&data.settings, &cfg->input, sizeof(_config_input));

	for (int i = PORT1; i < PORT_MAX; i++) {
		data.port[i].id = i + 1;
		memcpy(&data.port[i].port, &port[i], sizeof(_port));
	}

	setupUi(this);

	emu_pause(TRUE);

	js_update_detected_devices();

	setFont(parent->font());

	comboBox_cm->addItem(tr("NES"));
	comboBox_cm->addItem(tr("Famicom"));
	comboBox_cm->addItem(tr("Four Score"));
	comboBox_cm->setItemData(0, 0);
	comboBox_cm->setItemData(1, 1);
	comboBox_cm->setItemData(2, 2);
	connect(comboBox_cm, SIGNAL(activated(int)), this, SLOT(s_combobox_cm_activated(int)));

	combobox_cp_init();

	connect(checkBox_Permit_updown, SIGNAL(stateChanged(int)), this,
			SLOT(s_checkbox_permit_updown_leftright_changed(int)));
	connect(checkBox_Hide_Zapper_cursor, SIGNAL(stateChanged(int)), this,
			SLOT(s_checkbox_hide_zapper_cursor_changed(int)));

	setup_shortcuts();

	update_dialog();

	connect(pushButton_Default, SIGNAL(clicked(bool)), this, SLOT(s_default_clicked(bool)));
	connect(pushButton_Apply, SIGNAL(clicked(bool)), this, SLOT(s_apply_clicked(bool)));
	connect(pushButton_Discard, SIGNAL(clicked(bool)), this, SLOT(s_discard_clicked(bool)));

	shcut.timeout.timer = new QTimer(this);
	connect(shcut.timeout.timer, SIGNAL(timeout()), this, SLOT(s_button_timeout()));

	shcut.joy.timer = new QTimer(this);
	connect(shcut.joy.timer, SIGNAL(timeout()), this, SLOT(s_joy_read_timer()));

	setAttribute(Qt::WA_DeleteOnClose);
	setFixedSize(width(), height());

	installEventFilter(this);
}
dlgInput::~dlgInput() {}
bool dlgInput::eventFilter(QObject *obj, QEvent *event) {
	if (obj == this) {
		switch (event->type()) {
			case QEvent::Show:
				parentMain->ui->action_Input_Config->setEnabled(false);
				break;
			case QEvent::Close:
				shcut.timeout.timer->stop();
				shcut.joy.timer->stop();
#if defined (__linux__)
				if (shcut.joy.fd) {
					::close(shcut.joy.fd);
					shcut.joy.fd = 0;
				}
#endif
				parentMain->shcjoy_start();

				emu_pause(FALSE);

				shcut.no_other_buttons = false;

				parentMain->ui->action_Input_Config->setEnabled(true);
				break;
			case QEvent::LanguageChange:
				Input_dialog::retranslateUi(this);
				update_dialog();
				break;
			case QEvent::KeyPress:
				return (keypressEvent(event));
			default:
				break;
		}
	} else {
		// controllo il comportamento dello [Space] e dell'[Enter] dei QPushButton
		// che altrimenti non verrebbero mai intercettati perche' gestiti direttamente
		// dalle QT (nel caso di QPushButton).
		switch (event->type()) {
			case QEvent::KeyPress:
				if (shcut.no_other_buttons == true) {
					return (keypressEvent(event));
				}
				break;
			default:
				break;
		}
	}

	return (QObject::eventFilter(obj, event));
}
void dlgInput::update_dialog() {
	_cfg_port *ctrl_in;
	int index;

	groupBox_Ports->setEnabled(true);
	groupBox_Misc->setEnabled(true);

	// Controller Mode
	comboBox_cm->setCurrentIndex(data.settings.controller_mode);

	// Expansion Port
	for (index = 0; index < comboBox_exp->count(); index++) {
		if (data.settings.expansion == comboBox_exp->itemData(index).toInt()) {
			comboBox_exp->setCurrentIndex(index);
		}
	}
	switch (data.settings.controller_mode) {
		case CTRL_MODE_NES:
		case CTRL_MODE_FOUR_SCORE:
			comboBox_exp->setEnabled(false);
			break;
		case CTRL_MODE_FAMICOM:
			comboBox_exp->setEnabled(true);
			break;
	}

	// Ports
	for (int i = PORT1; i < PORT_MAX; i++) {
		ctrl_in = &data.port[i];
		QComboBox *cb = findChild<QComboBox *>(QString("comboBox_cp%1").arg(ctrl_in->id));
		QPushButton *pb = findChild<QPushButton *>(QString("pushButton_cp%1").arg(ctrl_in->id));
		bool mode = true, finded = false;

		for (index = 0; index < cb->count(); index++) {
			QList<QVariant> type = cb->itemData(index).toList();

			if (ctrl_in->port.type == type.at(0).toInt()) {
				finded = true;
				break;
			}
		}

		if (finded == false) {
			ctrl_in->port.type = CTRL_DISABLED;
			index = 0;
		}

		cb->setCurrentIndex(index);
		disconnect(pb, SIGNAL(clicked(bool)), this, SLOT(s_setup_clicked(bool)));

		switch (ctrl_in->port.type) {
			case CTRL_DISABLED:
			case CTRL_ZAPPER:
			case CTRL_SNES_MOUSE:
			case CTRL_ARKANOID_PADDLE:
			default:
				pb->setEnabled(false);
				break;
			case CTRL_STANDARD:
				pb->setEnabled(true);
				pb->setProperty("myPointer", QVariant::fromValue(((void *) ctrl_in)));
				connect(pb, SIGNAL(clicked(bool)), this, SLOT(s_setup_clicked(bool)));
				break;
		}

		if ((i >= PORT3) && (i <= PORT4)) {
			QLabel *lb = findChild<QLabel *>(QString("label_cp%1").arg(ctrl_in->id));

			switch (data.settings.controller_mode) {
				case CTRL_MODE_NES:
					mode = false;
					break;
				case CTRL_MODE_FAMICOM:
					if (data.settings.expansion != CTRL_STANDARD) {
						mode = false;
					}
					break;
				case CTRL_MODE_FOUR_SCORE:
					break;
			}

			lb->setEnabled(mode);
			cb->setEnabled(mode);

			if (mode == false) {
				pb->setEnabled(mode);
			}
		}
	}

	// Misc
	checkBox_Permit_updown->setChecked(data.settings.permit_updown_leftright);
	checkBox_Hide_Zapper_cursor->setChecked(data.settings.hide_zapper_cursor);

	// Shortcuts
	if (comboBox_joy_ID->count() > 1) {
		comboBox_joy_ID->setItemText(comboBox_joy_ID->count() - 1, tr("Disabled"));
	} else {
		comboBox_joy_ID->setItemText(comboBox_joy_ID->count() - 1, tr("No usable device"));
	}

	update_text_shortcut(parentMain->ui->action_Open, SET_INP_SC_OPEN);
	update_text_shortcut(parentMain->ui->action_Quit, SET_INP_SC_QUIT);
	update_text_shortcut(parentMain->ui->action_Turn_Off, SET_INP_SC_TURN_OFF);
	update_text_shortcut(parentMain->ui->action_Hard_Reset, SET_INP_SC_HARD_RESET);
	update_text_shortcut(parentMain->ui->action_Soft_Reset, SET_INP_SC_SOFT_RESET);
	update_text_shortcut(parentMain->ui->action_Insert_Coin, SET_INP_SC_INSERT_COIN);
	update_text_shortcut(parentMain->ui->action_Switch_sides, SET_INP_SC_SWITCH_SIDES);
	update_text_shortcut(parentMain->ui->action_Eject_Insert_Disk, SET_INP_SC_EJECT_DISK);
	update_text_shortcut(parentMain->ui->action_Start_Stop_WAV_recording, SET_INP_SC_WAV);
	update_text_shortcut(parentMain->ui->action_Fullscreen, SET_INP_SC_FULLSCREEN);
	update_text_shortcut(parentMain->ui->action_Pause, SET_INP_SC_PAUSE);
	update_text_shortcut(parentMain->ui->action_Fast_Forward, SET_INP_SC_FAST_FORWARD);
	update_text_shortcut(parentMain->ui->action_Save_Screenshot, SET_INP_SC_SCREENSHOT);
	update_text_shortcut(parentMain->ui->action_PAL, SET_INP_SC_MODE_PAL);
	update_text_shortcut(parentMain->ui->action_NTSC, SET_INP_SC_MODE_NTSC);
	update_text_shortcut(parentMain->ui->action_Dendy, SET_INP_SC_MODE_DENDY);
	update_text_shortcut(parentMain->ui->action_Mode_Auto, SET_INP_SC_MODE_AUTO);
	update_text_shortcut(parentMain->ui->action_1x, SET_INP_SC_SCALE_1X);
	update_text_shortcut(parentMain->ui->action_2x, SET_INP_SC_SCALE_2X);
	update_text_shortcut(parentMain->ui->action_3x, SET_INP_SC_SCALE_3X);
	update_text_shortcut(parentMain->ui->action_4x, SET_INP_SC_SCALE_4X);
	update_text_shortcut(parentMain->ui->action_5x, SET_INP_SC_SCALE_5X);
	update_text_shortcut(parentMain->ui->action_6x, SET_INP_SC_SCALE_6X);
	update_text_shortcut(parentMain->ui->action_Interpolation, SET_INP_SC_INTERPOLATION);
	update_text_shortcut(parentMain->ui->action_Stretch_in_fullscreen, SET_INP_SC_STRETCH_FULLSCREEN);
	update_text_shortcut(parentMain->ui->action_Audio_Enable, SET_INP_SC_AUDIO_ENABLE);
	update_text_shortcut(parentMain->ui->action_Save_settings, SET_INP_SC_SAVE_SETTINGS);
	update_text_shortcut(parentMain->ui->action_Save_state, SET_INP_SC_SAVE_STATE);
	update_text_shortcut(parentMain->ui->action_Load_state, SET_INP_SC_LOAD_STATE);
	update_text_shortcut(parentMain->ui->action_Rewind_state, SET_INP_SC_REWIND_STATE);
	update_text_shortcut(parentMain->ui->action_Increment_slot, SET_INP_SC_INC_SLOT);
	update_text_shortcut(parentMain->ui->action_Decrement_slot, SET_INP_SC_DEC_SLOT);

	pushButton_Default->setEnabled(true);
}
void dlgInput::combobox_cp_init(QComboBox *cb, _cfg_port *cfg_port, void *list, int length) {
	_cb_ports *cbp = (_cb_ports *) list;
	int i;

	cb->clear();

	for (i = 0; i < length; i++) {
		QList<QVariant> type;

		type.append(cbp[i].value);
		type.append(cfg_port->id - 1);
		type.append(QVariant::fromValue(((void *)cfg_port)));

		cb->addItem(cbp[i].desc);
		cb->setItemData(i, QVariant(type));
	}
	connect(cb, SIGNAL(activated(int)), this, SLOT(s_combobox_cp_activated(int)));
}
void dlgInput::combobox_cp_init() {
	// NES-001
	static const _cb_ports ctrl_mode_nes[] {
		{ tr("Disabled"),        CTRL_DISABLED },
		{ tr("Standard Pad"),    CTRL_STANDARD },
		{ tr("Zapper"),          CTRL_ZAPPER   },
		{ tr("Snes Mouse"),      CTRL_SNES_MOUSE },
		{ tr("Arkanoid Paddle"), CTRL_ARKANOID_PADDLE }
	};
	// Famicom
	static const _cb_ports ctrl_mode_famicom_expansion_port[] {
		{ tr("Standard Pad"),     CTRL_STANDARD },
		{ tr("Zapper"),           CTRL_ZAPPER   },
		{ tr("Arkanoid Paddle"),  CTRL_ARKANOID_PADDLE },
		{ tr("Oeka Kids Tablet"), CTRL_OEKA_KIDS_TABLET }
	};
	static const _cb_ports ctrl_mode_famicom_ports1[] {
		{ tr("Disabled"),        CTRL_DISABLED },
		{ tr("Standard Pad"),    CTRL_STANDARD },
		{ tr("Snes Mouse"),      CTRL_SNES_MOUSE }
	};
	static const _cb_ports ctrl_mode_famicom_ports2[] {
		{ tr("Disabled"),        CTRL_DISABLED },
		{ tr("Standard Pad"),    CTRL_STANDARD },
		{ tr("Snes Mouse"),      CTRL_SNES_MOUSE }
	};
	// Four Scoure
	static const _cb_ports ctrl_mode_four_score[] {
		{ tr("Disabled"),        CTRL_DISABLED },
		{ tr("Standard Pad"),    CTRL_STANDARD }
	};
	_cb_ports *ctrl_port1 = nullptr, *ctrl_port2 = nullptr;
	unsigned int i, length1 = 0, length2 = 0;

	switch (data.settings.controller_mode) {
		case CTRL_MODE_NES:
			ctrl_port1 = (_cb_ports *) &ctrl_mode_nes;
			ctrl_port2 = (_cb_ports *) &ctrl_mode_nes;
			length1 = length2 = LENGTH(ctrl_mode_nes);
			break;
		case CTRL_MODE_FAMICOM:
			ctrl_port1 = (_cb_ports *) &ctrl_mode_famicom_ports1;
			ctrl_port2 = (_cb_ports *) &ctrl_mode_famicom_ports2;
			length1 = LENGTH(ctrl_mode_famicom_ports1);
			length2 = LENGTH(ctrl_mode_famicom_ports2);
			break;
		case CTRL_MODE_FOUR_SCORE:
			ctrl_port1 = (_cb_ports *) &ctrl_mode_four_score;
			ctrl_port2 = (_cb_ports *) &ctrl_mode_four_score;
			length1 = length2 = LENGTH(ctrl_mode_four_score);
			break;
	}

	// Expansion Port
	comboBox_exp->clear();
	for (i = 0; i < LENGTH(ctrl_mode_famicom_expansion_port); i++) {
		comboBox_exp->addItem(ctrl_mode_famicom_expansion_port[i].desc);
		comboBox_exp->setItemData(i, ctrl_mode_famicom_expansion_port[i].value);
	}
	connect(comboBox_exp, SIGNAL(activated(int)), this, SLOT(s_combobox_cexp_activated(int)));

	// Ports
	combobox_cp_init(comboBox_cp1, &data.port[0], ctrl_port1, length1);
	combobox_cp_init(comboBox_cp2, &data.port[1], ctrl_port2, length2);
	combobox_cp_init(comboBox_cp3, &data.port[2], (void *) ctrl_mode_four_score,
			LENGTH(ctrl_mode_four_score));
	combobox_cp_init(comboBox_cp4, &data.port[3], (void *) ctrl_mode_four_score,
			LENGTH(ctrl_mode_four_score));
}
void dlgInput::setup_shortcuts(void) {
	QFont f9;

	f9.setPointSize(9);
	f9.setWeight(QFont::Light);

	parentMain->shcjoy_stop();

	shcut.joy.fd = 0;
	shcut.no_other_buttons = false;

	for (int a = 0; a < SET_MAX_NUM_SC; a++) {
		for (int b = 0; b < 2; b++) {
			shcut.text[b] << "";
		}
		populate_shortcut(a + SET_INP_SC_OPEN);
	}

	combo_joy_id_init();

	shcut.bckColor = tableWidget_Shortcuts->item(0, 0)->background();

	if (plainTextEdit_input_info->font().pointSize() > 9) {
		plainTextEdit_input_info->setFont(f9);
	}

	update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
}
void dlgInput::combo_joy_id_init() {
	BYTE disabled_line = 0, count = 0, current_line = name_to_jsn(uL("NULL"));

	comboBox_joy_ID->clear();

	for (int a = 0; a <= MAX_JOYSTICK; a++) {
		BYTE id = a;

		if (a < MAX_JOYSTICK) {
			if (js_is_connected(id) == EXIT_ERROR) {
				continue;
			}

			if (js_is_this(id, &data.settings.shcjoy_id)) {
				current_line = count;
			}

			comboBox_joy_ID->addItem(QString("js%1: ").arg(id) + uQString(js_name_device(id)));
		} else {
			if (count == 0) {
				break;
			}
			comboBox_joy_ID->addItem("Disabled");
			id = name_to_jsn(uL("NULL"));
			disabled_line = count;
		}

		comboBox_joy_ID->setItemData(count, id);
		count++;
	}

	if (count == 0) {
		comboBox_joy_ID->addItem("No usable device");
	}

	if (count > 0) {
		if (js_is_null(&data.settings.shcjoy_id) || (current_line == name_to_jsn(uL("NULL")))) {
			comboBox_joy_ID->setCurrentIndex(disabled_line);
		} else {
			comboBox_joy_ID->setCurrentIndex(current_line);
		}
		connect(comboBox_joy_ID, SIGNAL(activated(int)), this,
				SLOT(s_combobox_joy_activated(int)));
	} else {
		comboBox_joy_ID->setCurrentIndex(0);
	}
}
void dlgInput::update_groupbox_shortcuts(int mode, int type, int row) {
	for (int i = 0; i < SET_MAX_NUM_SC; i++) {
		if (shcut.text[KEYBOARD].at(i + SET_INP_SC_OPEN).isEmpty()) {
			continue;
		}

		switch (mode) {
			case UPDATE_ALL: {
				QWidget *widget;
				bool joy_mode = false;
				BYTE joyId;

				if (comboBox_joy_ID->count() > 1) {
					joy_mode = true;
				}

				label_joy_ID->setEnabled(joy_mode);
				comboBox_joy_ID->setEnabled(joy_mode);

				tableWidget_Shortcuts->item(i, 0)->setBackground(shcut.bckColor);

				widget = tableWidget_Shortcuts->cellWidget(i, 1);
				widget->setEnabled(true);
				widget->findChild<QPushButton *>("value")->setEnabled(true);
				widget->findChild<QPushButton *>("default")->setEnabled(true);

				tableWidget_Shortcuts->cellWidget(i, 1)->setEnabled(true);

				joyId = comboBox_joy_ID->itemData(comboBox_joy_ID->currentIndex()).toInt();

				if ((comboBox_joy_ID->count() > 1) && (joyId != name_to_jsn(uL("NULL")))) {
					joy_mode = true;
				} else {
					joy_mode = false;
				}

				widget = tableWidget_Shortcuts->cellWidget(i, 2);
				widget->setEnabled(joy_mode);
				widget->findChild<QPushButton *>("value")->setEnabled(joy_mode);
				widget->findChild<QPushButton *>("unset")->setEnabled(joy_mode);

				break;
			}
			case BUTTON_PRESSED: {
				QWidget *widget;
				BYTE joyId;

				groupBox_Ports->setEnabled(false);
				groupBox_Misc->setEnabled(false);
				pushButton_Default->setEnabled(false);

				label_joy_ID->setEnabled(false);
				comboBox_joy_ID->setEnabled(false);

				if (row == i) {
					tableWidget_Shortcuts->item(i, 0)->setBackground(Qt::cyan);
				}

				if ((type == KEYBOARD) && (row == i)) {
					widget = tableWidget_Shortcuts->cellWidget(i, 1);
					widget->setEnabled(true);
					widget->findChild<QPushButton *>("default")->setEnabled(false);
				} else {
					tableWidget_Shortcuts->cellWidget(i, 1)->setEnabled(false);
				}

				joyId = comboBox_joy_ID->itemData(comboBox_joy_ID->currentIndex()).toInt();

				if ((comboBox_joy_ID->count() > 1) && (joyId != name_to_jsn(uL("NULL")))) {
					if ((type == JOYSTICK) && (row == i)) {
						widget = tableWidget_Shortcuts->cellWidget(i, 2);
						widget->setEnabled(true);
						widget->findChild<QPushButton *>("unset")->setEnabled(false);
					} else {
						tableWidget_Shortcuts->cellWidget(i, 2)->setEnabled(false);
					}
				}

				break;
			}
		}
	}
}
void dlgInput::populate_shortcut(int index) {
	int row = index - SET_INP_SC_OPEN;
	QTableWidgetItem *col;
	QHBoxLayout *layout;
	QWidget *widget;
	QPushButton *btext, *bicon;

	tableWidget_Shortcuts->insertRow(row);

	col = new QTableWidgetItem();
	col->setTextAlignment(Qt::AlignCenter);
	tableWidget_Shortcuts->setItem(row, 0, col);

	// keyboard
	shcut.text[KEYBOARD].replace(row, (QString(*(QString *)settings_inp_rd_sc(index, KEYBOARD))));
	widget = new QWidget(this);
	layout = new QHBoxLayout(this);
	btext = new QPushButton(this);
	bicon = new QPushButton(this);
	btext->setObjectName(QString::fromUtf8("value"));
	btext->setProperty("myValue", QVariant(row));
	btext->setProperty("myType", QVariant(KEYBOARD));
	btext->installEventFilter(this);
	connect(btext, SIGNAL(clicked(bool)), this, SLOT(s_shortcut_clicked(bool)));
	bicon->setObjectName(QString::fromUtf8("default"));
	bicon->setIcon(QIcon(":/icon/icons/default.png"));
	bicon->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	bicon->setMinimumSize(0, 0);
	bicon->setMaximumSize(30, btext->sizeHint().height());
	bicon->setToolTip(tr("Default"));
	bicon->setProperty("myValue", QVariant(row));
	connect(bicon, SIGNAL(clicked(bool)), this, SLOT(s_keyb_shortcut_default(bool)));
	layout->addWidget(btext);
	layout->addWidget(bicon);
	layout->setContentsMargins(0,0,0,0);
	layout->setSpacing(0);
	widget->setLayout(layout);
	tableWidget_Shortcuts->setCellWidget(row, 1, widget);

	// joystick
	shcut.text[JOYSTICK].replace(row, (QString(*(QString *)settings_inp_rd_sc(index, JOYSTICK))));
	widget = new QWidget(this);
	layout = new QHBoxLayout(this);
	btext = new QPushButton(this);
	bicon = new QPushButton(this);
	btext->setObjectName(QString::fromUtf8("value"));
	btext->setProperty("myValue", QVariant(row));
	btext->setProperty("myType", QVariant(JOYSTICK));
	btext->installEventFilter(this);
	connect(btext, SIGNAL(clicked(bool)), this, SLOT(s_shortcut_clicked(bool)));
	bicon->setObjectName(QString::fromUtf8("unset"));
	bicon->setIcon(QIcon(":/icon/icons/trash.png"));
	bicon->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	bicon->setMinimumSize(0, 0);
	bicon->setMaximumSize(30, btext->sizeHint().height());
	bicon->setToolTip(tr("Unset"));
	bicon->setProperty("myValue", QVariant(row));
	connect(bicon, SIGNAL(clicked(bool)), this, SLOT(s_joy_shortcut_unset(bool)));
	layout->addWidget(btext);
	layout->addWidget(bicon);
	layout->setContentsMargins(0,0,0,0);
	layout->setSpacing(0);
	widget->setLayout(layout);
	tableWidget_Shortcuts->setCellWidget(row, 2, widget);
}
void dlgInput::update_text_shortcut(QAction *action, int index) {
	QStringList text = action->text().remove('&').split('\t');
	int row = index - SET_INP_SC_OPEN;

	/* action */
	if (index == SET_INP_SC_WAV) {
		tableWidget_Shortcuts->item(row, 0)->setText(tr("Start/Stop WAV"));
	} else {
		tableWidget_Shortcuts->item(row, 0)->setText(text.at(0));
	}

	/* keyboard */
	tableWidget_Shortcuts->cellWidget(row, 1)->findChild<QPushButton *>("value")->setText(
			shcut.text[KEYBOARD].at(row));

	/* joystick */
	tableWidget_Shortcuts->cellWidget(row, 2)->findChild<QPushButton *>("value")->setText(
			shcut.text[JOYSTICK].at(row));
}
void dlgInput::info_entry_print(QString txt) {
	plainTextEdit_input_info->setPlainText(txt);
}
bool dlgInput::keypressEvent(QEvent *event) {
	QKeyEvent *keyEvent = ((QKeyEvent *)event);

	if (shcut.no_other_buttons == false) {
		return (true);
	}

	if (shcut.type == KEYBOARD) {
		if ((keyEvent->key() == Qt::Key_Control) || (keyEvent->key() == Qt::Key_Meta)  ||
			(keyEvent->key() == Qt::Key_Alt) || (keyEvent->key() == Qt::Key_AltGr) ||
			(keyEvent->key() == Qt::Key_Shift)) {
			return (true);
		}

		if (keyEvent->key() != Qt::Key_Escape) {
			QString key = QKeySequence(keyEvent->key()).toString(QKeySequence::PortableText);

			switch (keyEvent->modifiers()) {
				case Qt::GroupSwitchModifier:
				case Qt::NoModifier:
				default:
					shcut.text[KEYBOARD].replace(shcut.row, key);
					break;
				case Qt::ControlModifier:
					shcut.text[KEYBOARD].replace(shcut.row, QString("Ctrl+%1").arg(key));
					break;
				case Qt::AltModifier:
					shcut.text[KEYBOARD].replace(shcut.row, QString("Alt+%1").arg(key));
					break;
				case Qt::MetaModifier:
					shcut.text[KEYBOARD].replace(shcut.row, QString("Meta+%1").arg(key));
					break;
			}
		}
	} else {
		// quando sto configurando il joystick, l'unico input da tastiera che accetto e' l'escape
		if (keyEvent->key() != Qt::Key_Escape) {
			return (true);
		}
		shcut.joy.timer->stop();
	}

	shcut.timeout.timer->stop();
	info_entry_print("");

	update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
	update_dialog();

	shcut.no_other_buttons = false;

	return (true);
}
void dlgInput::s_combobox_cm_activated(int index) {
	int type = comboBox_cm->itemData(index).toInt();

	data.settings.controller_mode = type;
	combobox_cp_init();
	update_dialog();
}
void dlgInput::s_combobox_cexp_activated(int index) {
	int type = comboBox_exp->itemData(index).toInt();

	data.settings.expansion = type;
	combobox_cp_init();
	update_dialog();
}
void dlgInput::s_combobox_cp_activated(int index) {
	QList<QVariant> type = ((QComboBox *)sender())->itemData(index).toList();
	_cfg_port *cfg_port = ((_cfg_port *)type.at(2).value<void *>());

	cfg_port->port.type = type.at(0).toInt();
	update_dialog();
}
void dlgInput::s_setup_clicked(bool checked) {
	_cfg_port *cfg_port = ((_cfg_port *) ((QPushButton *) sender())->property("myPointer").value<void *>());

	switch (cfg_port->port.type) {
		case CTRL_DISABLED:
		case CTRL_ZAPPER:
			break;
		case CTRL_STANDARD:
			dlgStdPad *dlg = new dlgStdPad(cfg_port, this);

			hide();
			dlg->exec();
			show();

			combo_joy_id_init();
			update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
			update_dialog();
			break;
	}
}
void dlgInput::s_checkbox_permit_updown_leftright_changed(int state) {
	if (state) {
		data.settings.permit_updown_leftright = true;
	} else {
		data.settings.permit_updown_leftright = false;
	}
}
void dlgInput::s_checkbox_hide_zapper_cursor_changed(int state) {
	if (state) {
		data.settings.hide_zapper_cursor = true;
	} else {
		data.settings.hide_zapper_cursor = false;
	}
}
void dlgInput::s_combobox_joy_activated(int index) {
	unsigned int id = ((QComboBox *)sender())->itemData(index).toInt();

	js_set_id(&data.settings.shcjoy_id, id);
	update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
}
void dlgInput::s_shortcut_clicked(bool checked) {
	if (shcut.no_other_buttons == true) {
		return;
	}

	shcut.type = QVariant(((QObject *)sender())->property("myType")).toInt();
	shcut.row = QVariant(((QObject *)sender())->property("myValue")).toInt();
	shcut.bp = ((QPushButton *)sender());

	update_groupbox_shortcuts(BUTTON_PRESSED, shcut.type, shcut.row);

	shcut.no_other_buttons = true;
	shcut.bp->setText("...");

	shcut.bp->setFocus(Qt::ActiveWindowFocusReason);

	shcut.timeout.seconds = 5;
	shcut.timeout.timer->start(1000);
	s_button_timeout();

	if (shcut.type == JOYSTICK) {
#if defined (__linux__)
		_js_event jse;
		ssize_t size = sizeof(jse);
		char device[30];

		::sprintf(device, "%s%d", JS_DEV_PATH, data.settings.shcjoy_id);
		shcut.joy.fd = ::open(device, O_RDONLY | O_NONBLOCK);

		if (shcut.joy.fd < 0) {
			info_entry_print(tr("Error on open device %1").arg(device));
			update_dialog();
			return;
		}

		for (int i = 0; i < MAX_JOYSTICK; i++) {
			if (::read(shcut.joy.fd, &jse, size) < 0) {
				info_entry_print(tr("Error on reading controllers configurations"));
			}
		}

		/* svuoto il buffer iniziale */
		for (int i = 0; i < 10; i++) {
			if (::read(shcut.joy.fd, &jse, size) < 0) {
				;
			}
		}
		shcut.joy.value = 0;
		shcut.joy.timer->start(30);
#elif defined (__WIN32__)
		shcut.joy.value = 0;
		shcut.joy.timer->start(150);
#endif
	}
}
void dlgInput::s_keyb_shortcut_default(bool checked) {
	int row = QVariant(((QObject *)sender())->property("myValue")).toInt();

	shcut.text[KEYBOARD].replace(row,
			uQString(inp_cfg[row + SET_INP_SC_OPEN].def).split(",").at(KEYBOARD));
	tableWidget_Shortcuts->cellWidget(row, 1)->findChild<QPushButton *>("value")->setText(
			shcut.text[KEYBOARD].at(row));
}
void dlgInput::s_joy_shortcut_unset(bool checked) {
	int row = QVariant(((QObject *)sender())->property("myValue")).toInt();

	shcut.text[JOYSTICK].replace(row, "NULL");
	tableWidget_Shortcuts->cellWidget(row, 2)->findChild<QPushButton *>("value")->setText("NULL");
}
void dlgInput::s_joy_read_timer() {
	DBWORD value = js_read_in_dialog(&data.settings.shcjoy_id, shcut.joy.fd);

	if (shcut.joy.value && !value) {
#if defined (__linux__)
		::close(shcut.joy.fd);
		shcut.joy.fd = 0;
#endif
		shcut.text[JOYSTICK].replace(shcut.row, uQString(jsv_to_name(shcut.joy.value)));

		shcut.timeout.timer->stop();
		shcut.joy.timer->stop();
		info_entry_print("");

		update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
		update_dialog();

		shcut.no_other_buttons = false;
	}

	shcut.joy.value = value;
}
void dlgInput::s_button_timeout() {
	info_entry_print(tr("Press a key (ESC for the previous value \"%1\") - timeout in %2").arg(
			shcut.text[shcut.type].at(shcut.row), QString::number(shcut.timeout.seconds--)));

	if (shcut.timeout.seconds < 0) {
		QKeyEvent *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);

		QCoreApplication::postEvent(shcut.bp, event);
	}
}
void dlgInput::s_default_clicked(bool checked) {
	_array_pointers_port array;

	for (int i = PORT1; i < PORT_MAX; i++) {
		array.port[i] = &data.port[i].port;
	}

	settings_inp_all_default(&data.settings, &array);

	js_set_id(&data.settings.shcjoy_id, name_to_jsn(uL("NULL")));

	comboBox_joy_ID->setCurrentIndex(comboBox_joy_ID->count() - 1);
	for (int i = 0; i < SET_MAX_NUM_SC; i++) {
		shcut.text[KEYBOARD].replace(i,
				uQString(inp_cfg[i + SET_INP_SC_OPEN].def).split(",").at(KEYBOARD));
		shcut.text[JOYSTICK].replace(i,
				uQString(inp_cfg[i + SET_INP_SC_OPEN].def).split(",").at(JOYSTICK));
	}

	update_groupbox_shortcuts(UPDATE_ALL, NO_ACTION, NO_ACTION);
	update_dialog();
}
void dlgInput::s_apply_clicked(bool checked) {
	memcpy(&cfg->input, &data.settings, sizeof(_config_input));

	for (int i = PORT1; i < PORT_MAX; i++) {
		if (data.port[i].port.type != port[i].type) {
			for (int a = TRB_A; a <= TRB_B; a++) {
				int type = a - TRB_A;

				data.port[i].port.turbo[type].active = 0;
				data.port[i].port.turbo[type].counter = 0;
			}
		}
		memcpy(&port[i], &data.port[i].port, sizeof(_port));
	}

	for (int i = 0; i < SET_MAX_NUM_SC; i++) {
		if (shcut.text[KEYBOARD].at(i).isEmpty() == false) {
			settings_inp_wr_sc((void *)&shcut.text[KEYBOARD].at(i), i + SET_INP_SC_OPEN, KEYBOARD);
			settings_inp_wr_sc((void *)&shcut.text[JOYSTICK].at(i), i + SET_INP_SC_OPEN, JOYSTICK);
		}
	}

	parentMain->shortcuts();

	// Faccio l'update del menu per i casi dello zapper e degli effetti
	gui_update();

	settings_inp_save();

	input_init(SET_CURSOR);

	js_quit(FALSE);
	js_init(FALSE);

	close();
}
void dlgInput::s_discard_clicked(bool checked) {
	close();
}
