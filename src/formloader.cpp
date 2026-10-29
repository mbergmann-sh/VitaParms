// SPDX-License-Identifier: MIT
#include "formloader.h"
#include "qtcompat.h"
#include "appsettings.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <algorithm>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollBar>
#include <QScrollArea>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimeEdit>
#ifndef VP_NO_UITOOLS
#include <QUiLoader>
#endif

namespace FormLoader {

static QString tr(const char *s) { return QCoreApplication::translate("FormLoader", s); }

QList<FormInfo> forms()
{
    return {
        { QStringLiteral("patientform"), tr("Stammblatt"),
          { QStringLiteral("lastNameEdit") },
          { QStringLiteral("lastNameEdit"), QStringLiteral("firstNameEdit"),
            QStringLiteral("birthDateEdit"), QStringLiteral("sexCombo"),
            QStringLiteral("streetEdit"), QStringLiteral("zipEdit"), QStringLiteral("cityEdit"),
            QStringLiteral("phoneEdit"), QStringLiteral("mobileEdit"),
            QStringLiteral("diagnosesEdit"), QStringLiteral("notesEdit"),
            QStringLiteral("armLeftCombo"), QStringLiteral("armRightCombo"),
            QStringLiteral("legLeftCombo"), QStringLiteral("legRightCombo"),
            QStringLiteral("amputationInfoLabel"),
            QStringLiteral("careLevelCombo"), QStringLiteral("careReviewEdit"),
            QStringLiteral("disabilityDegreeSpin"), QStringLiteral("disabilityMarksBox"),
            QStringLiteral("disabilityValidEdit"), QStringLiteral("disabilityUnlimitedCheck"),
            QStringLiteral("medicationTable"), QStringLiteral("addMedicationButton"),
            QStringLiteral("editMedicationButton"), QStringLiteral("archiveMedicationButton"),
            QStringLiteral("deleteMedicationButton"), QStringLiteral("showArchivedCheck"),
            QStringLiteral("printMedicationButton"),
            QStringLiteral("admissionLabel"), QStringLiteral("admissionButton") } },
        { QStringLiteral("vitalform"), tr("Vitalwerte-Erfassung"),
          { QStringLiteral("dateTimeEdit") },
          { QStringLiteral("headerNameLabel"), QStringLiteral("headerFirstNameLabel"),
            QStringLiteral("headerBirthDateLabel"),
            QStringLiteral("dateTimeEdit"), QStringLiteral("nowButton"),
            QStringLiteral("contextCombo"),
            QStringLiteral("pulseSpin"), QStringLiteral("systolicSpin"),
            QStringLiteral("diastolicSpin"), QStringLiteral("temperatureSpin"),
            QStringLiteral("spo2Spin"), QStringLiteral("peakFlowSpin"), QStringLiteral("glucoseSpin"),
            QStringLiteral("glucoseUnitLabel"), QStringLiteral("weightSpin"),
            QStringLiteral("heightSpin"), QStringLiteral("bmiValueLabel"),
            QStringLiteral("bmiInfoLabel"), QStringLiteral("observationsEdit") } },
        { QStringLiteral("medicationform"), tr("Medikament"),
          { QStringLiteral("nameEdit") },
          { QStringLiteral("nameEdit"), QStringLiteral("substanceEdit"),
            QStringLiteral("strengthEdit"), QStringLiteral("formCombo"),
            QStringLiteral("morningEdit"), QStringLiteral("noonEdit"),
            QStringLiteral("eveningEdit"), QStringLiteral("nightEdit"),
            QStringLiteral("unitCombo"), QStringLiteral("reasonEdit"),
            QStringLiteral("notesEdit"), QStringLiteral("startDateEdit"),
            QStringLiteral("endDateCheck"), QStringLiteral("endDateEdit"),
            QStringLiteral("archivedCheck") } },
        { QStringLiteral("documentform"), tr("Dokument (Krankenakte)"),
          { QStringLiteral("titleEdit") },
          { QStringLiteral("dateEdit"), QStringLiteral("categoryCombo"), QStringLiteral("titleEdit"),
            QStringLiteral("issuerEdit"), QStringLiteral("notesEdit"), QStringLiteral("fileInfoLabel"),
            QStringLiteral("textInfoLabel"), QStringLiteral("previewLabel") } },
        { QStringLiteral("appointmentform"), tr("Termin"),
          { QStringLiteral("titleEdit"), QStringLiteral("startEdit") },
          { QStringLiteral("titleEdit"), QStringLiteral("startEdit"), QStringLiteral("allDayCheck"),
            QStringLiteral("endEdit"), QStringLiteral("locationEdit"), QStringLiteral("patientCombo"),
            QStringLiteral("ownerCombo"), QStringLiteral("priorityCombo"), QStringLiteral("notesEdit"),
            QStringLiteral("remindCombo"), QStringLiteral("remindVisualCheck"),
            QStringLiteral("remindSoundCheck"), QStringLiteral("remindEmailCheck"),
            QStringLiteral("emailToEdit"), QStringLiteral("reminderHintLabel"), QStringLiteral("doneCheck") } },
    };
}

std::optional<FormInfo> info(const QString &name)
{
    for (const FormInfo &f : forms())
        if (f.name == name)
            return f;
    return std::nullopt;
}

QString builtinPath(const QString &name)
{
    return QStringLiteral(":/forms/%1.ui").arg(name);
}

QString userPath(const QString &name)
{
    return QDir(AppSettings::userFormsDir()).filePath(name + QStringLiteral(".ui"));
}

static QString portablePath(const QString &name)
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("forms/%1.ui").arg(name));
}

bool hasUserVersion(const QString &name)
{
    return QFile::exists(userPath(name));
}

int formVersion(QWidget *root)
{
    return root ? root->property("formVersion").toInt() : 0;
}

static QWidget *loadFile(const QString &path, QWidget *parent, QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return nullptr;
    }
#ifndef VP_NO_UITOOLS
    QUiLoader loader;
    QWidget *w = loader.load(&f, parent);
    if (!w && err)
        *err = loader.errorString();
    return w;
#else
    Q_UNUSED(parent)
    if (err) *err = QStringLiteral("QtUiTools not available");
    return nullptr;
#endif
}

static QStringList missingRequired(QWidget *root, const QStringList &required)
{
    QStringList missing;
    for (const QString &n : required)
        if (!root->findChild<QWidget *>(n) && root->objectName() != n)
            missing << n;
    return missing;
}

QWidget *load(const QString &name, QWidget *parent, QString *origin)
{
    static QSet<QString> warnedThisSession;

    const auto fi = info(name);
    const QStringList required = fi ? fi->required : QStringList();
    const QStringList candidates = { userPath(name), portablePath(name) };

    QWidget *builtinForVersion = nullptr;
    for (const QString &path : candidates) {
        if (!QFile::exists(path))
            continue;
        QString err;
        QWidget *w = loadFile(path, parent, &err);
        if (!w) {
            if (!warnedThisSession.contains(path)) {
                warnedThisSession.insert(path);
                QMessageBox::warning(parent, tr("Formular"),
                    tr("Das angepasste Formular\n%1\nkonnte nicht geladen werden (%2).\n"
                       "Es wird die Standardversion verwendet.").arg(path, err));
            }
            continue;
        }
        const QStringList missing = missingRequired(w, required);
        if (!missing.isEmpty()) {
            delete w;
            if (!warnedThisSession.contains(path)) {
                warnedThisSession.insert(path);
                QMessageBox::warning(parent, tr("Formular"),
                    tr("Im angepassten Formular\n%1\nfehlen zwingend benötigte Elemente:\n%2\n\n"
                       "Es wird die Standardversion verwendet.")
                        .arg(path, missing.join(QStringLiteral(", "))));
            }
            continue;
        }
        // Versionshinweis: Standardformular neuer als die Anpassung?
        if (!warnedThisSession.contains(path + QStringLiteral("#v"))) {
            warnedThisSession.insert(path + QStringLiteral("#v"));
            builtinForVersion = loadFile(builtinPath(name), nullptr, nullptr);
            const int vb = formVersion(builtinForVersion);
            const int vu = formVersion(w);
            delete builtinForVersion;
            if (vu < vb) {
                QMessageBox::information(parent, tr("Formular"),
                    tr("Das angepasste Formular „%1“ basiert auf einer älteren Vorlage "
                       "(Version %2, aktuell %3). Neue Eingabefelder fehlen darin "
                       "möglicherweise.\n\nÜber „Extras → Formulare anpassen…“ können Sie "
                       "die aktuelle Vorlage erneut übernehmen.")
                        .arg(fi ? fi->title : name).arg(vu).arg(vb));
            }
        }
        if (origin) *origin = path;
        return w;
    }

    QString err;
    QWidget *w = loadFile(builtinPath(name), parent, &err);
    if (!w)
        qFatal("Eingebettetes Formular %s fehlt: %s", qPrintable(name), qPrintable(err));
    if (origin) *origin = builtinPath(name);
    return w;
}

QScrollArea *scrollable(QWidget *form, QWidget *parent)
{
    auto *sa = new QScrollArea(parent);
    sa->setObjectName(QStringLiteral("formScroll"));
    sa->setFrameShape(QFrame::NoFrame);
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    sa->setWidget(form);
    // Breite des Formulars übernehmen, damit nicht unnötig waagerecht gescrollt wird
    sa->setMinimumWidth(std::min(form->minimumSizeHint().width() + sa->verticalScrollBar()->sizeHint().width() + 4, 1200));
    return sa;
}

void fitToScreen(QWidget *w)
{
    QScreen *scr = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    if (w->parentWidget() && w->parentWidget()->screen()) scr = w->parentWidget()->screen();
    if (!scr) return;
    const QRect avail = scr->availableGeometry();
    const int maxW = avail.width() - 40, maxH = avail.height() - 60;   // Rahmen und Titelleiste
    QSize sz = w->size();
    // zu klein (z. B. gespeicherte Größe einer älteren Version): so groß wie das Formular, höchstens Bildschirm
    if (auto *sa = w->findChild<QScrollArea *>(QStringLiteral("formScroll")); sa && sa->widget()) {
        // Rand um den Scrollbereich (Knöpfe, Abstände) aus den Größenvorschlägen, nicht aus der
        // aktuellen Größe – die steht vor dem ersten Anzeigen noch nicht fest
        const int extraH = std::max(0, w->sizeHint().height() - sa->sizeHint().height());
        const int extraW = std::max(0, w->sizeHint().width() - sa->sizeHint().width());
        const int wantH = std::min(sa->widget()->sizeHint().height() + extraH + 4, maxH);
        const int wantW = std::min(sa->widget()->minimumSizeHint().width() + extraW + sa->verticalScrollBar()->sizeHint().width() + 4, maxW);
        if (sz.height() < wantH) sz.setHeight(wantH);
        if (sz.width() < wantW) sz.setWidth(wantW);
    }
    sz = QSize(std::min(sz.width(), maxW), std::min(sz.height(), maxH));
    if (sz != w->size()) w->resize(sz);
    QRect g = w->frameGeometry();
    if (!avail.contains(g) && w->isVisible()) {
        g.moveLeft(std::clamp(g.left(), avail.left(), std::max(avail.left(), avail.right() - g.width())));
        g.moveTop(std::clamp(g.top(), avail.top(), std::max(avail.top(), avail.bottom() - g.height())));
        w->move(g.topLeft());
    }
}

bool copyToUser(const QString &name, QString *error)
{
    QDir().mkpath(AppSettings::userFormsDir());
    const QString target = userPath(name);
    if (QFile::exists(target) && !QFile::remove(target)) {
        if (error) *error = tr("Vorhandene Datei kann nicht ersetzt werden.");
        return false;
    }
    if (!QFile::copy(builtinPath(name), target)) {
        if (error) *error = tr("Kopieren fehlgeschlagen.");
        return false;
    }
    // Ressourcen sind schreibgeschützt – Kopie beschreibbar machen
    QFile::setPermissions(target, QFile::permissions(target)
                                      | QFileDevice::WriteOwner | QFileDevice::ReadOwner);
    return true;
}

bool removeUserVersion(const QString &name, QString *error)
{
    if (!hasUserVersion(name))
        return true;
    if (!QFile::remove(userPath(name))) {
        if (error) *error = tr("Datei kann nicht gelöscht werden.");
        return false;
    }
    return true;
}

QStringList check(const QString &name, const QString &filePath)
{
    QStringList msgs;
    QString err;
    QWidget *w = loadFile(filePath, nullptr, &err);
    if (!w)
        return { tr("Datei kann nicht geladen werden: %1").arg(err) };

    const auto fi = info(name);
    if (fi) {
        const QStringList missing = missingRequired(w, fi->required);
        if (!missing.isEmpty())
            msgs << tr("Pflichtelemente fehlen: %1").arg(missing.join(QStringLiteral(", ")));
        QStringList optionalMissing;
        for (const QString &n : fi->known)
            if (!fi->required.contains(n) && !w->findChild<QWidget *>(n))
                optionalMissing << n;
        if (!optionalMissing.isEmpty())
            msgs << tr("Nicht vorhanden (Feld wird nicht bearbeitet): %1")
                        .arg(optionalMissing.join(QStringLiteral(", ")));
    }
    const QStringList extras = FormIO::extraFieldNames(w);
    if (!extras.isEmpty())
        msgs << tr("Zusatzfelder (vpField): %1").arg(extras.join(QStringLiteral(", ")));
    delete w;
    return msgs;
}

} // namespace FormLoader

// ======================================================================
namespace FormIO {

static QWidget *find(QWidget *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    return root->findChild<QWidget *>(name);
}

bool has(QWidget *root, const QString &objectName)
{
    return find(root, objectName) != nullptr;
}

QVariant readWidget(QWidget *w)
{
    if (auto *e = qobject_cast<QLineEdit *>(w))      return e->text();
    if (auto *e = qobject_cast<QPlainTextEdit *>(w)) return e->toPlainText();
    if (auto *e = qobject_cast<QTextEdit *>(w))      return e->toPlainText();
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        if (c->isEditable()) return c->currentText();
        const QVariant d = c->currentData();
        return d.isValid() ? d : QVariant(c->currentText());
    }
    if (auto *s = qobject_cast<QSpinBox *>(w)) {
        if (!s->specialValueText().isEmpty() && s->value() == s->minimum()) return {};
        return s->value();
    }
    if (auto *s = qobject_cast<QDoubleSpinBox *>(w)) {
        if (!s->specialValueText().isEmpty() && qFuzzyCompare(s->value(), s->minimum())) return {};
        return s->value();
    }
    // Reihenfolge wichtig: QDateEdit/QTimeEdit sind von QDateTimeEdit abgeleitet
    if (auto *d = qobject_cast<QDateEdit *>(w)) {
        if (!d->specialValueText().isEmpty() && d->date() == d->minimumDate()) return {};
        return d->date();
    }
    if (auto *t = qobject_cast<QTimeEdit *>(w)) return t->time();
    if (auto *d = qobject_cast<QDateTimeEdit *>(w)) {
        if (!d->specialValueText().isEmpty() && d->dateTime() == d->minimumDateTime()) return {};
        return d->dateTime();
    }
    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        if (b->isCheckable()) return b->isChecked();
        return {};
    }
    if (auto *s = qobject_cast<QSlider *>(w))  return s->value();
    if (auto *l = qobject_cast<QLabel *>(w))   return l->text();
    return {};
}

void writeWidget(QWidget *w, const QVariant &v)
{
    if (auto *e = qobject_cast<QLineEdit *>(w))      { e->setText(v.toString()); return; }
    if (auto *e = qobject_cast<QPlainTextEdit *>(w)) { e->setPlainText(v.toString()); return; }
    if (auto *e = qobject_cast<QTextEdit *>(w))      { e->setPlainText(v.toString()); return; }
    if (auto *c = qobject_cast<QComboBox *>(w)) {
        int i = v.isValid() ? c->findData(v) : -1;
        if (i < 0) i = c->findText(v.toString());
        if (i >= 0) c->setCurrentIndex(i);
        else if (c->isEditable()) c->setEditText(v.toString());
        else if (!v.isValid() || v.toString().isEmpty()) c->setCurrentIndex(-1);
        return;
    }
    if (auto *s = qobject_cast<QSpinBox *>(w)) {
        s->setValue(v.isValid() && !v.isNull() ? v.toInt() : s->minimum());
        return;
    }
    if (auto *s = qobject_cast<QDoubleSpinBox *>(w)) {
        s->setValue(v.isValid() && !v.isNull() ? v.toDouble() : s->minimum());
        return;
    }
    if (auto *d = qobject_cast<QDateEdit *>(w)) {
        QDate dt = v.toDate();
        if (!dt.isValid()) dt = QDate::fromString(v.toString(), Qt::ISODate);
        d->setDate(dt.isValid() ? dt : d->minimumDate());
        return;
    }
    if (auto *t = qobject_cast<QTimeEdit *>(w)) {
        QTime tm = v.toTime();
        if (!tm.isValid()) tm = QTime::fromString(v.toString(), Qt::ISODate);
        if (tm.isValid()) t->setTime(tm);
        return;
    }
    if (auto *d = qobject_cast<QDateTimeEdit *>(w)) {
        QDateTime dt = v.toDateTime();
        if (!dt.isValid()) dt = QDateTime::fromString(v.toString(), Qt::ISODate);
        d->setDateTime(dt.isValid() ? dt : d->minimumDateTime());
        return;
    }
    if (auto *b = qobject_cast<QAbstractButton *>(w)) {
        if (b->isCheckable()) b->setChecked(v.toBool());
        return;
    }
    if (auto *s = qobject_cast<QSlider *>(w)) { s->setValue(v.toInt()); return; }
    if (auto *l = qobject_cast<QLabel *>(w))  { l->setText(v.toString()); return; }
}

std::optional<QVariant> read(QWidget *root, const QString &objectName)
{
    QWidget *w = find(root, objectName);
    if (!w) return std::nullopt;
    return readWidget(w);
}

bool write(QWidget *root, const QString &objectName, const QVariant &value)
{
    QWidget *w = find(root, objectName);
    if (!w) return false;
    writeWidget(w, value);
    return true;
}

static QList<QWidget *> extraWidgets(QWidget *root)
{
    QList<QWidget *> l;
    if (!root) return l;
    const auto all = root->findChildren<QWidget *>();
    for (QWidget *w : all)
        if (!w->property("vpField").toString().trimmed().isEmpty())
            l << w;
    return l;
}

QStringList extraFieldNames(QWidget *root)
{
    QStringList l;
    for (QWidget *w : extraWidgets(root))
        l << w->property("vpField").toString().trimmed();
    return l;
}

static QJsonValue toJson(const QVariant &v)
{
    if (!v.isValid() || v.isNull()) return QJsonValue(QJsonValue::Null);
    switch (vpTypeId(v)) {
    case QMetaType::Bool:    return v.toBool();
    case QMetaType::Int:
    case QMetaType::LongLong: return v.toLongLong();
    case QMetaType::Double:  return v.toDouble();
    case QMetaType::QDate:   return v.toDate().toString(Qt::ISODate);
    case QMetaType::QTime:   return v.toTime().toString(Qt::ISODate);
    case QMetaType::QDateTime: return v.toDateTime().toString(Qt::ISODate);
    default:                 return v.toString();
    }
}

QJsonObject collectExtra(QWidget *root, const QJsonObject &previous)
{
    // Werte von Feldern, die (nicht mehr) im Formular sind, bleiben erhalten.
    QJsonObject o = previous;
    for (QWidget *w : extraWidgets(root)) {
        const QString key = w->property("vpField").toString().trimmed();
        const QJsonValue jv = toJson(readWidget(w));
        if (jv.isNull() || (jv.isString() && jv.toString().isEmpty()))
            o.remove(key);
        else
            o.insert(key, jv);
    }
    return o;
}

void applyExtra(QWidget *root, const QJsonObject &values)
{
    for (QWidget *w : extraWidgets(root)) {
        const QString key = w->property("vpField").toString().trimmed();
        writeWidget(w, values.value(key).toVariant());
    }
}

// Beispielfelder der Standardformulare – nur für lupdate, gespeichert wird immer der deutsche Schlüssel
static const char *const kBuiltinFields[] = {
    QT_TRANSLATE_NOOP("VpField", "Atemfrequenz"),
    QT_TRANSLATE_NOOP("VpField", "Schmerz (NRS)"),
    QT_TRANSLATE_NOOP("VpField", "Peak-Flow"),
    QT_TRANSLATE_NOOP("VpField", "Krankenkasse"),
    QT_TRANSLATE_NOOP("VpField", "Hausarztpraxis"),
    QT_TRANSLATE_NOOP("VpField", "Praxis-Telefon"),
    QT_TRANSLATE_NOOP("VpField", "Notfallkontakt"),
    QT_TRANSLATE_NOOP("VpField", "Allergien"),
};

QString fieldLabel(const QString &key)
{
    for (const char *k : kBuiltinFields)
        if (key == QLatin1String(k))
            return QCoreApplication::translate("VpField", k);
    return key;
}

QStringList fieldLabels(const QStringList &keys)
{
    QStringList l;
    for (const QString &k : keys) l << fieldLabel(k);
    return l;
}

} // namespace FormIO

