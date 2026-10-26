// SPDX-License-Identifier: MIT
#include "formcustomizedialog.h"
#include "appsettings.h"
#include "formloader.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

FormCustomizeDialog::FormCustomizeDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Formulare anpassen"));
    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(tr(
        "Die Eingabeformulare von VitaParms sind Qt-Designer-Dateien (.ui) und werden beim "
        "Öffnen geladen. Sie können Felder umanordnen, umbenennen, entfernen oder eigene "
        "Felder hinzufügen.<br><br>"
        "<b>Eigene Felder:</b> Im Qt Designer beim neuen Eingabeelement eine dynamische "
        "Eigenschaft <tt>vpField</tt> (Typ String) hinzufügen und den Feldnamen eintragen. "
        "Der Wert wird automatisch gespeichert und im Verlauf/Export als eigene Spalte "
        "angezeigt.<br>"
        "<b>Nicht ändern:</b> die Objektnamen (<tt>objectName</tt>) der Standardfelder."), this);
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);

    m_list = new QTreeWidget(this);
    m_list->setColumnCount(3);
    m_list->setHeaderLabels({ tr("Formular"), tr("Status"), tr("Datei") });
    m_list->setRootIsDecorated(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_list, &QTreeWidget::itemSelectionChanged, this, &FormCustomizeDialog::updateButtons);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, &FormCustomizeDialog::openInDesigner);
    lay->addWidget(m_list);

    auto *row = new QHBoxLayout;
    m_customize = new QPushButton(tr("&Anpassen"), this);
    m_customize->setToolTip(tr("Legt eine bearbeitbare Kopie der Standardvorlage im Formularordner an."));
    m_designer = new QPushButton(tr("In Qt &Designer öffnen"), this);
    m_check = new QPushButton(tr("&Prüfen"), this);
    m_preview = new QPushButton(tr("&Vorschau"), this);
    m_reset = new QPushButton(tr("Auf &Standard zurücksetzen"), this);
    auto *folder = new QPushButton(tr("&Ordner öffnen"), this);
    for (QPushButton *b : { m_customize, m_designer, m_check, m_preview, m_reset, folder })
        row->addWidget(b);
    lay->addLayout(row);
    connect(m_customize, &QPushButton::clicked, this, &FormCustomizeDialog::customize);
    connect(m_designer, &QPushButton::clicked, this, &FormCustomizeDialog::openInDesigner);
    connect(m_check, &QPushButton::clicked, this, &FormCustomizeDialog::checkForm);
    connect(m_preview, &QPushButton::clicked, this, &FormCustomizeDialog::preview);
    connect(m_reset, &QPushButton::clicked, this, &FormCustomizeDialog::resetForm);
    connect(folder, &QPushButton::clicked, this, &FormCustomizeDialog::openFolder);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(140);
    m_log->setPlaceholderText(tr("Prüfergebnisse erscheinen hier."));
    lay->addWidget(m_log);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(tr("&Schließen"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    refresh();
    resize(780, 520);
}

QString FormCustomizeDialog::findDesigner()
{
    QStringList candidates;
    const QString bin = QLibraryInfo::path(QLibraryInfo::BinariesPath);
#if defined(Q_OS_MACOS)
    candidates << bin + QStringLiteral("/Designer.app/Contents/MacOS/Designer");
#elif defined(Q_OS_WIN)
    candidates << bin + QStringLiteral("/designer.exe");
#else
    candidates << bin + QStringLiteral("/designer");
    candidates << QStringLiteral("/usr/lib/qt6/bin/designer");
#endif
    for (const QString &c : candidates)
        if (QFileInfo(c).isExecutable())
            return c;
    for (const char *n : { "designer6", "designer-qt6", "designer" }) {
        const QString p = QStandardPaths::findExecutable(QLatin1String(n));
        if (!p.isEmpty()) return p;
    }
    return {};
}

void FormCustomizeDialog::refresh()
{
    const QString sel = currentName();
    m_list->clear();
    for (const auto &f : FormLoader::forms()) {
        auto *it = new QTreeWidgetItem(m_list);
        const bool custom = FormLoader::hasUserVersion(f.name);
        it->setText(0, f.title);
        it->setText(1, custom ? tr("angepasst") : tr("Standard"));
        it->setText(2, custom ? QDir::toNativeSeparators(FormLoader::userPath(f.name))
                              : tr("(im Programm enthalten)"));
        it->setData(0, Qt::UserRole, f.name);
        if (custom) { QFont fn = it->font(1); fn.setBold(true); it->setFont(1, fn); }
        if (f.name == sel) m_list->setCurrentItem(it);
    }
    if (!m_list->currentItem() && m_list->topLevelItemCount())
        m_list->setCurrentItem(m_list->topLevelItem(0));
    updateButtons();
}

QString FormCustomizeDialog::currentName() const
{
    auto *it = m_list ? m_list->currentItem() : nullptr;
    return it ? it->data(0, Qt::UserRole).toString() : QString();
}

void FormCustomizeDialog::updateButtons()
{
    const QString n = currentName();
    const bool custom = !n.isEmpty() && FormLoader::hasUserVersion(n);
    m_customize->setEnabled(!n.isEmpty() && !custom);
    m_designer->setEnabled(!n.isEmpty());
    m_check->setEnabled(!n.isEmpty());
    m_preview->setEnabled(!n.isEmpty());
    m_reset->setEnabled(custom);
}

void FormCustomizeDialog::customize()
{
    const QString n = currentName();
    QString err;
    if (!FormLoader::copyToUser(n, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_log->appendPlainText(tr("Kopie angelegt: %1").arg(QDir::toNativeSeparators(FormLoader::userPath(n))));
    refresh();
}

void FormCustomizeDialog::openInDesigner()
{
    const QString n = currentName();
    if (n.isEmpty()) return;
    if (!FormLoader::hasUserVersion(n)) {
        if (QMessageBox::question(this, windowTitle(),
                tr("Für dieses Formular gibt es noch keine angepasste Version.\n"
                   "Jetzt eine Kopie der Standardvorlage anlegen und öffnen?")) != QMessageBox::Yes)
            return;
        customize();
        if (!FormLoader::hasUserVersion(n)) return;
    }
    const QString designer = findDesigner();
    const QString file = FormLoader::userPath(n);
    if (designer.isEmpty() || !QProcess::startDetached(designer, { file })) {
        QMessageBox::information(this, windowTitle(),
            tr("Qt Designer wurde nicht gefunden.\n\nBitte öffnen Sie die Datei\n%1\n"
               "manuell mit Qt Designer (Bestandteil der Qt-Installation bzw. des Pakets "
               "„qt6-tools“/„designer“).").arg(QDir::toNativeSeparators(file)));
        return;
    }
    m_log->appendPlainText(tr("Qt Designer gestartet. Nach dem Speichern wird das Formular "
                              "beim nächsten Öffnen des Dialogs verwendet."));
}

void FormCustomizeDialog::checkForm()
{
    const QString n = currentName();
    const QString path = FormLoader::hasUserVersion(n) ? FormLoader::userPath(n) : FormLoader::builtinPath(n);
    const QStringList msgs = FormLoader::check(n, path);
    m_log->appendPlainText(QStringLiteral("— %1 —").arg(QDir::toNativeSeparators(path)));
    if (msgs.isEmpty())
        m_log->appendPlainText(tr("OK: alle Standardfelder vorhanden."));
    for (const QString &m : msgs)
        m_log->appendPlainText(QStringLiteral("• ") + m);
}

void FormCustomizeDialog::preview()
{
    const QString n = currentName();
    QDialog d(this);
    d.setWindowTitle(tr("Vorschau"));
    auto *l = new QVBoxLayout(&d);
    QString origin;
    l->addWidget(FormLoader::load(n, &d, &origin));
    auto *info = new QLabel(tr("<small>Quelle: %1</small>").arg(QDir::toNativeSeparators(origin).toHtmlEscaped()), &d);
    l->addWidget(info);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &d);
    connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    l->addWidget(bb);
    d.exec();
}

void FormCustomizeDialog::resetForm()
{
    const QString n = currentName();
    if (QMessageBox::question(this, windowTitle(),
            tr("Die angepasste Version wird gelöscht und wieder die Standardvorlage verwendet.\n"
               "Bereits gespeicherte Werte von Zusatzfeldern bleiben in der Datenbank erhalten.\n\n"
               "Fortfahren?")) != QMessageBox::Yes)
        return;
    QString err;
    if (!FormLoader::removeUserVersion(n, &err))
        QMessageBox::warning(this, windowTitle(), err);
    refresh();
}

void FormCustomizeDialog::openFolder()
{
    QDir().mkpath(AppSettings::userFormsDir());
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::userFormsDir()));
}
