// VitaParms – Laden benutzerveränderlicher Formulare (.ui) zur Laufzeit
// SPDX-License-Identifier: MIT
//
// Suchreihenfolge für <name>.ui:
//   1. Benutzer-Formularordner  (AppSettings::userFormsDir())
//   2. <Programmordner>/forms    (für portable Installationen)
//   3. eingebettete Standardversion  (:/forms/<name>.ui)
//
// Fehlt in einer angepassten Datei ein zwingend benötigtes Element oder lässt
// sie sich nicht laden, wird automatisch auf die Standardversion zurückgegriffen.
//
// Zusatzfelder: Jedes Eingabeelement mit der dynamischen Eigenschaft
// "vpField" (String) wird automatisch gespeichert und geladen – ohne
// Programmänderung. Der Eigenschaftswert ist der Feldname.
#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <optional>

class QWidget;

namespace FormLoader {

struct FormInfo {
    QString     name;        // Dateiname ohne .ui
    QString     title;       // Anzeigename
    QStringList required;    // zwingend benötigte objectNames
    QStringList known;       // alle vom Programm ausgewerteten objectNames
};

QList<FormInfo> forms();
std::optional<FormInfo> info(const QString &name);

// Lädt das Formular; 'origin' erhält den tatsächlich verwendeten Pfad.
QWidget *load(const QString &name, QWidget *parent, QString *origin = nullptr);

QString builtinPath(const QString &name);
QString userPath(const QString &name);
bool    hasUserVersion(const QString &name);
bool    copyToUser(const QString &name, QString *error = nullptr);
bool    removeUserVersion(const QString &name, QString *error = nullptr);
int     formVersion(QWidget *root);

// Prüft eine .ui-Datei und liefert eine Liste von Meldungen (leer = alles ok)
QStringList check(const QString &name, const QString &filePath);

} // namespace FormLoader

// Typunabhängiger Zugriff auf Formularelemente über ihren objectName.
// So bleibt das Programm funktionsfähig, wenn ein Benutzer z. B. ein
// QLineEdit gegen ein QPlainTextEdit austauscht oder ein Feld entfernt.
namespace FormIO {

bool has(QWidget *root, const QString &objectName);

// nullopt  = Element fehlt im Formular  (Wert unverändert lassen)
// QVariant() = Element vorhanden, aber leer
std::optional<QVariant> read(QWidget *root, const QString &objectName);
bool write(QWidget *root, const QString &objectName, const QVariant &value);

QVariant readWidget(QWidget *w);
void     writeWidget(QWidget *w, const QVariant &value);

// Hilfsfunktionen
inline QString readString(QWidget *root, const QString &name, const QString &fallback)
{
    const auto v = read(root, name);
    return v ? v->toString() : fallback;
}

QJsonObject collectExtra(QWidget *root, const QJsonObject &previous = {});
void        applyExtra(QWidget *root, const QJsonObject &values);
QStringList extraFieldNames(QWidget *root);

// Anzeigename eines Zusatzfelds: Die Beispielfelder der Standardformulare werden
// übersetzt (Kontext "VpField"); eigene Felder erscheinen unverändert.
QString fieldLabel(const QString &key);
QStringList fieldLabels(const QStringList &keys);

} // namespace FormIO
