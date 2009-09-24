/* copyright (C) 2005-2009, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "RichTextEditor.h"
#include "Global.h"
#include "MainWindow.h"

RichTextEditorLink::RichTextEditorLink(const QString &txt, QWidget *p) : QDialog(p) {
	setupUi(this);
	
	if (! txt.isEmpty()) {
		qleText->setText(txt);
	}
}

QString RichTextEditorLink::text() const {
	QUrl url(qleUrl->text(), QUrl::StrictMode);
	QString text = qleText->text();
	
	if (url.isValid() && ! url.isRelative() && ! text.isEmpty()) {
		return QString::fromLatin1("<a href=\"%1\">%2</a>").arg(QLatin1String(url.toEncoded()), Qt::escape(text));
	}
	
	return QString();
}

RichTextEditor::RichTextEditor(QWidget *p) : QTabWidget(p) {
	bChanged = false;
	bModified = false;

	setupUi(this);
	
	qtbToolBar->addAction(qaBold);
	qtbToolBar->addAction(qaItalic);
	qtbToolBar->addAction(qaUnderline);
	qtbToolBar->addAction(qaColor);
	qtbToolBar->addSeparator();
	qtbToolBar->addAction(qaLink);
	qtbToolBar->addAction(qaImage);
	
	connect(this, SIGNAL(currentChanged(int)), this, SLOT(onCurrentChanged(int)));
	updateActions();
	
	qteRichText->setFocus();
}

bool RichTextEditor::isModified() const {
	return bModified;
}

void RichTextEditor::on_qaBold_triggered(bool on) {
	qteRichText->setFontWeight(on ? QFont::Bold : QFont::Normal);
}

void RichTextEditor::on_qaItalic_triggered(bool on) {
	qteRichText->setFontItalic(on);
}

void RichTextEditor::on_qaUnderline_triggered(bool on) {
	qteRichText->setFontUnderline(on);
}

void RichTextEditor::on_qaColor_triggered() {
	QColor c = QColorDialog::getColor();
	if (c.isValid())
		qteRichText->setTextColor(c);
}

void RichTextEditor::on_qaLink_triggered() {
	QTextCursor qtc = qteRichText->textCursor();
	RichTextEditorLink *rtel = new RichTextEditorLink(qtc.selectedText(), this);
	if (rtel->exec() == QDialog::Accepted) {
		QString html = rtel->text();
		if (! html.isEmpty())
			qteRichText->insertHtml(html);
	}
	delete rtel;
}

void RichTextEditor::on_qaImage_triggered() {
	QPair<QByteArray, QImage> choice = g.mw->openImageFile();
	
	QByteArray &qba = choice.first;
	
	if (qba.isEmpty())
		return;
		
	if (qba.length() > 65536) {
		QMessageBox::warning(this, tr("Failed to load image"), tr("Image file to large to embed in document. Please use images smaller than %1 kB.").arg(65536/1024));
		return;
	}
	
	QBuffer qb(&qba);
	qb.open(QIODevice::ReadOnly);
	
	QString format = QLatin1String(QImageReader::imageFormat(&qb));
	qb.close();
	
	if (format.isEmpty())
		format = QLatin1String("qt");
		
	QByteArray rawbase = qba.toBase64();
	QByteArray encoded;
	int i = 0;
	int begin = 0, end = 0;
	do {
		begin = i*72;
		end = begin+72;

		encoded.append(QUrl::toPercentEncoding(QLatin1String(rawbase.mid(begin,72))));
		if (end < rawbase.length())
			encoded.append('\n');

		++i;
	} while (end < rawbase.length());

	QString link = QString::fromLatin1("<img src=\"data:image/%1;base64,%2\" />").arg(format).arg(QLatin1String(encoded));
	qteRichText->insertHtml(link);
}

void RichTextEditor::onCurrentChanged(int index) {
	if (! bChanged)
		return;

	if (index == 1)
		richToPlain();
	else
		qteRichText->setHtml(qptePlainText->toPlainText());
		
	bChanged = false;
}

void RichTextEditor::on_qptePlainText_textChanged() {
	bModified = true;
	bChanged = true;
}

void RichTextEditor::on_qteRichText_textChanged() {
	bModified = true;
	bChanged = true;
	updateActions();
}

void RichTextEditor::on_qteRichText_cursorPositionChanged() {
	updateActions();
}

void RichTextEditor::on_qteRichText_currentCharFormatChanged() {
	updateActions();
}

void RichTextEditor::updateColor(const QColor &col) {
	if (col == qcColor)
		return;
	qcColor = col;
	
	QRect r(0,0,24,24);

	QPixmap qpm(r.size());
	QPainter qp(&qpm);
	qp.fillRect(r, col);
	qp.setPen(col.darker());
	qp.drawRect(r.adjusted(0, 0, -1, -1));

	qaColor->setIcon(qpm);
}

void RichTextEditor::updateActions() {
    qaBold->setChecked(qteRichText->fontWeight() == QFont::Bold);
    qaItalic->setChecked(qteRichText->fontItalic());
    qaUnderline->setChecked(qteRichText->fontUnderline());
    updateColor(qteRichText->textColor());
}

/* Recursively parse and output XHTML.
 * This will drop <head>, <html> etc, take the contents of <body> and strip out unnecesarry styles.
 * It will also change <span style=""> into matching <b>, <i> or <u>.
 */

static void recurseParse(QXmlStreamReader &reader, QXmlStreamWriter &writer, int &paragraphs, const QMap<QString, QString> &opstyle = QMap<QString, QString>(), const int close = 0, bool ignore = true) {
	while(! reader.atEnd()) {
		QXmlStreamReader::TokenType tt = reader.readNext();

		QXmlStreamAttributes a = reader.attributes();
		QMap<QString, QString> style;
		QMap<QString, QString> pstyle = opstyle;

		if (a.hasAttribute(QLatin1String("style"))) {
			QString stylestring = a.value(QLatin1String("style")).toString();
			QStringList styles = stylestring.split(QLatin1String(";"), QString::SkipEmptyParts);
			foreach(QString s, styles) {
				s = s.simplified();
				int idx = s.indexOf(QLatin1Char(':'));
				QString key = (idx > 0) ? s.left(idx).simplified() : s;
				QString val = (idx > 0) ? s.mid(idx+1).simplified() : QString();
				
				if (! pstyle.contains(key) || (pstyle.value(key) != val)) {
					style.insert(key,val);
					pstyle.insert(key,val);
				}
			}
		}

		switch(tt) {
			case QXmlStreamReader::StartElement:
				{
					QString name = reader.name().toString();
					int rclose = 1;
					if (name == QLatin1String("body")) {
						rclose = 0;
						ignore = false;
					} else if (name == QLatin1String("span")) {
						// Substitute style with <b>, <i> and <u>
						
						rclose = 0;
						if (style.value(QLatin1String("font-weight")) == QLatin1String("600")) {
							writer.writeStartElement(QLatin1String("b"));
							rclose++;
							style.remove(QLatin1String("font-weight"));
						}
						if (style.value(QLatin1String("font-style")) == QLatin1String("italic")) {
							writer.writeStartElement(QLatin1String("i"));
							rclose++;
							style.remove(QLatin1String("font-style"));
						}
						if (style.value(QLatin1String("text-decoration")) == QLatin1String("underline")) {
							writer.writeStartElement(QLatin1String("u"));
							rclose++;
							style.remove(QLatin1String("text-decoration"));
						}
						if (! style.isEmpty()) {
							rclose++;
							writer.writeStartElement(name);
							
							QStringList qsl;
							QMap<QString, QString>::const_iterator i;
							for(i=style.constBegin(); i != style.constEnd(); ++i) {
								if (! i.value().isEmpty())
									qsl << QString::fromLatin1("%1:%2").arg(i.key(), i.value());
								else
									qsl << i.key();
							}
							
							writer.writeAttribute(QLatin1String("style"), qsl.join(QLatin1String("; ")));
						}
					} else if (name == QLatin1String("p")) {
						// Ignore first paragraph.

						paragraphs++;
						if (paragraphs > 1) {
							rclose = 1;
							writer.writeStartElement(name);

							if (! style.isEmpty()) {
								QStringList qsl;
								QMap<QString, QString>::const_iterator i;
								for(i=style.constBegin(); i != style.constEnd(); ++i) {
									if (! i.value().isEmpty())
										qsl << QString::fromLatin1("%1:%2").arg(i.key(), i.value());
									else
										qsl << i.key();
								}

								writer.writeAttribute(QLatin1String("style"), qsl.join(QLatin1String("; ")));
							}
						} else {
							rclose = 0;
						}
					} else if (name == QLatin1String("a")) {
						// Set pstyle to include implicit blue underline.
						rclose = 1;
						writer.writeCurrentToken(reader);
						pstyle.insert(QLatin1String("text-decoration"), QLatin1String("underline"));
						pstyle.insert(QLatin1String("color"), QLatin1String("#0000ff"));
					} else if (! ignore) {
						rclose = 1;
						writer.writeCurrentToken(reader);
					}

					recurseParse(reader, writer, paragraphs, pstyle, rclose, ignore);
					break;
				}
			case QXmlStreamReader::EndElement:
				if (!ignore)
					for(int i=0;i<close;++i)
						writer.writeEndElement();
				return;
			case QXmlStreamReader::Characters:
				if (! ignore)
					writer.writeCharacters(reader.text().toString());
				break;
		}
	}
}

/* Iterate XML and remove close-followed-by-open.
 * For example, make "<b>bold with </b><b><i>italic</i></b>" into
 * "<b>bold with <i>italic</i></b>"
 */

static bool unduplicateTags(QXmlStreamReader &reader, QXmlStreamWriter &writer) {
	bool changed = false;
	bool needclose = false;
	
	QStringList qslConcat;
	qslConcat << QLatin1String("b");
	qslConcat << QLatin1String("i");
	qslConcat << QLatin1String("u");
	qslConcat << QLatin1String("a");
	
	QList<QString> qlNames;
	QList<QXmlStreamAttributes> qlAttributes;

	while(! reader.atEnd()) {
		QXmlStreamReader::TokenType tt = reader.readNext();
		QString name = reader.name().toString();
		switch (tt) {
			case QXmlStreamReader::StartDocument:
			case QXmlStreamReader::EndDocument:
				break;
			case QXmlStreamReader::StartElement:
				{
					QXmlStreamAttributes a = reader.attributes();
					
					if (name == QLatin1String("unduplicate"))
						break;

					if (needclose) {
						needclose = false;

						if ((a == qlAttributes.last()) && (name == qlNames.last()) && (qslConcat.contains(name))) {
							changed = true;
							break;
						}
						qlNames.takeLast();
						qlAttributes.takeLast();
						writer.writeEndElement();
					}
					writer.writeCurrentToken(reader);
					qlNames.append(name);
					qlAttributes.append(a);
				}
				break;
			case QXmlStreamReader::EndElement:
				{
					if (name == QLatin1String("unduplicate"))
						break;
					if (needclose) {
						qlNames.takeLast();
						qlAttributes.takeLast();
						writer.writeCurrentToken(reader);
					} else {
						needclose = true;
					}
					needclose = true;
				}
				break;
			default:
				if (needclose) {
					writer.writeEndElement();
					needclose = false;
				}
				writer.writeCurrentToken(reader);
		}
	}
	if (needclose) 
		writer.writeEndElement();
	return changed;
}

void RichTextEditor::richToPlain() {
	QXmlStreamReader reader(qteRichText->toHtml());
	
	QString qsOutput;
	QXmlStreamWriter writer(&qsOutput);
	
	int paragraphs = 0;
	
	QMap<QString, QString> def;

	def.insert(QLatin1String("margin-top"), QLatin1String("0px"));
	def.insert(QLatin1String("margin-bottom"), QLatin1String("0px"));
	def.insert(QLatin1String("margin-left"), QLatin1String("0px"));
	def.insert(QLatin1String("margin-right"), QLatin1String("0px"));
	def.insert(QLatin1String("-qt-block-indent"), QLatin1String("0"));
	def.insert(QLatin1String("text-indent"), QLatin1String("0px"));
	
	recurseParse(reader, writer, paragraphs, def);
	
	qsOutput = qsOutput.trimmed();
	
	bool changed;
	do {
		qsOutput = QString::fromLatin1("<unduplicate>%1</unduplicate>").arg(qsOutput);

		QXmlStreamReader r(qsOutput);
		qsOutput = QString();
		QXmlStreamWriter w(&qsOutput);
		changed = unduplicateTags(r, w);
		qsOutput = qsOutput.trimmed();
	} while (changed);
	
	qptePlainText->setPlainText(qsOutput);
}

void RichTextEditor::setText(const QString &txt) {
	qptePlainText->setPlainText(txt);

	if (Qt::mightBeRichText(txt))
		qteRichText->setHtml(txt);
	else
		qteRichText->setPlainText(txt);
		
	bChanged = false;
	bModified = false;
}

QString RichTextEditor::text() {
	if (bChanged) {
		if (currentIndex() == 0)
			richToPlain();
		else
			qteRichText->setHtml(qptePlainText->toPlainText());
	}

	bChanged = false;
	return qptePlainText->toPlainText();
}