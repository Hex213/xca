/* vi: set sw=4 ts=4:
 *
 * Copyright (C) 2001 - 2010 Christian Hohnstaedt.
 *
 * All rights reserved.
 */


#include "lib/main.h"
#include "lib/pki_evp.h"
#include "lib/pki_scard.h"

#include "KeyDetail.h"
#include "MainWindow.h"
#include "Help.h"
#include "distname.h"
#include "clicklabel.h"
#include "XcaApplication.h"
#include "OidResolver.h"

#include <QLabel>
#include <QPushButton>
#include <QLineEdit>

#include <stdio.h>

KeyDetail::KeyDetail(QWidget *w)
	: QDialog(w ? w : mainwin) , keySqlId()
{
	setupUi(this);
	setWindowTitle(XCA_TITLE);
	image->setPixmap(QPixmap(":keyImg"));
	mainwin->helpdlg->register_ctxhelp_button(this, "keydetail");

	keyModulus->setFont(XcaApplication::tableFont);
	tabWidget->setCurrentIndex(0);

	Database.connectToDbChangeEvt(this, SLOT(itemChanged(pki_base*)));
}

#ifndef OPENSSL_NO_EC
static QString CurveComment(int nid)
{
	foreach(builtin_curve curve, builtinCurves) {
		if (curve.nid == nid)
			return curve.comment;
	}
	return QString();
}
#endif

void KeyDetail::setupFingerprints(pki_key *key)
{
	int pos = 0;
	QWidget *widget = new QWidget(fingerprint);
	QVBoxLayout *v = new QVBoxLayout(fingerprint);
	QGridLayout *grid = new QGridLayout(widget);
	v->addStretch();
	v->addWidget(widget);
	v->addStretch();

	QStringList sl; sl <<
		"ssh MD5" << "ssh SHA256 B64" <<
		"x509 SHA1" << "DER SHA256";

	foreach(QString type, sl) {
		qDebug() << type << key->fingerprint(type);

		QLabel *left = new QLabel(widget);
		CopyLabel *right = new CopyLabel(widget);

		left->setTextFormat(Qt::PlainText);
		left->setText(type);
		right->setText(key->fingerprint(type));

		grid->addWidget(left, pos, 0);
		grid->addWidget(right, pos, 1);
		pos++;
	}
}

void KeyDetail::setKey(pki_key *key)
{
	keySqlId = key->getSqlItemId();
	keyDesc->setText(key->getIntName());
	keyLength->setText(key->length());

	keyPrivEx->disableToolTip();
	if (!key->isToken())
		tabWidget->removeTab(1);
	tlHeader->setText(tr("Details of the %1 key").arg(key->getTypeString()));
	comment->setPlainText(key->getComment());

	setupFingerprints(key);

	if (key->isPubKey()) {
		keyPrivEx->setText(tr("Not available"));
		keyPrivEx->setRed();
	} else if (key->isToken()) {
		image->setPixmap(QPixmap(":scardImg"));
		pki_scard *card = static_cast<pki_scard *>(key);
		cardLabel->setText(card->getCardLabel());
		cardModel->setText(card->getModel());
		cardManufacturer->setText(card->getManufacturer());
		cardSerial->setText(card->getSerial());
		slotLabel->setText(card->getLabel());
		cardId->setText(card->getId());
		keyPrivEx->setText(tr("Security token"));
	} else {
		keyPrivEx->setText(tr("Available"));
		keyPrivEx->setGreen();
	}
	switch (key->getKeyType()) {
		case EVP_PKEY_RSA:
			keyPubEx->setText(key->pubEx());
			keyModulus->setText(key->modulus());
			break;
		case EVP_PKEY_DSA:
			tlPubEx->setText(tr("Sub prime"));
			tlModulus->setTitle(tr("Public key"));
			tlPrivEx->setText(tr("Private key"));
			keyPubEx->setText(key->subprime());
			keyModulus->setText(key->pubkey());
			break;
		case EVP_PKEY_FALCON512:
		case EVP_PKEY_FALCON1024:
		case EVP_PKEY_DILITHIUM2:
		case EVP_PKEY_DILITHIUM3:
		case EVP_PKEY_DILITHIUM5:
			tlPubEx->setText(tr("none"));
			tlModulus->setTitle(tr("Public key"));
			tlPrivEx->setText(tr("Private key"));
			keyPubEx->setText(key->subprime());
			
			auto* buffer = new unsigned char[key->GetSize() + 1];
			BIO *bio = BIO_new(BIO_s_mem());
			int lenkey = EVP_PKEY_print_public(bio, key->GetKey(), 0, NULL);
			BIO_free(bio);
			bio = BIO_new(BIO_s_mem());
			delete [] buffer;

			buffer = new unsigned char[lenkey];
			unsigned char* buf = buffer;
			EVP_PKEY_print_public(bio, key->GetKey(), 0, NULL);
			auto lenbuff = BIO_get_mem_data(bio, &buf);
			
			keyModulus->setText(tr((char*)buf));
			break;
#ifndef OPENSSL_NO_EC
		case EVP_PKEY_EC:
			int nid;
			nid = key->ecParamNid();
			tlModulus->setTitle(tr("Public key"));
			tlPrivEx->setText(tr("Private key"));
			tlPubEx->setText(tr("Curve name"));
			keyPubEx->setText(OBJ_nid2sn(nid));
			connect(keyPubEx, SIGNAL(doubleClicked(QString)),
				MainWindow::getResolver(),
				SLOT(searchOid(QString)));
			keyPubEx->setToolTip(CurveComment(nid));
			keyModulus->setText(key->ecPubKey());
			break;
#ifdef EVP_PKEY_ED25519
		case EVP_PKEY_ED25519:
			tlModulus->setTitle(tr("Public key"));
			tlPrivEx->setText(tr("Private key"));
			tlPubEx->setText(tr("Curve name"));
			keyPubEx->setText("ed25519");
			keyModulus->setText(key->ed25519PubKey().toHex());
			break;
#endif
#endif
		default:
			tlHeader->setText(tr("Unknown key"));
	}
}

void KeyDetail::itemChanged(pki_base *pki)
{
	if (pki->getSqlItemId() == keySqlId)
		keyDesc->setText(pki->getIntName());
}

void KeyDetail::showKey(QWidget *parent, pki_key *key, bool ro)
{
	if (!key)
		return;
	KeyDetail *dlg = new KeyDetail(parent);
	if (!dlg)
		return;
	dlg->setKey(key);
	dlg->keyDesc->setReadOnly(ro);
	dlg->comment->setReadOnly(ro);
	if (dlg->exec()) {
		db_base *db = Database.modelForPki(key);
		if (!db) {
			key->setIntName(dlg->keyDesc->text());
			key->setComment(dlg->comment->toPlainText());
		} else {
			db->updateItem(key, dlg->keyDesc->text(),
					dlg->comment->toPlainText());
		}
	}
        delete dlg;
}
