// VitaParms – Beipackzettel (Gebrauchsinformation) im Internet nachschlagen und anzeigen
//
// Quellen:
//   * Webseiten (im Browser): Apotheken Umschau per PZN, Gelbe Liste, Websuche – plus eigene
//     Quellen als URL-Vorlage mit {pzn}, {pzn7}, {name}, {substance}, {slug}
//   * direkt in VitaParms: EMA-Produktinformation (PDF mit Packungsbeilage, nur zentral
//     zugelassene Arzneimittel), openFDA (US-Arzneimittelinformation, englisch, gemeinfrei),
//     eine beliebige PDF-Adresse oder Datei (z. B. die Original-Gebrauchsinformation)
// Ein abgerufener Beipackzettel kann in der (verschlüsselten) Krankenakte gespeichert und mit
// dem Medikament verknüpft werden – dann ist er auch ohne Internet verfügbar.
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDialog>
#include <QPointer>
#include <QUrl>
#include <memory>

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QStackedWidget;
class QTemporaryFile;
class QTextBrowser;
class QPdfDocument;
class QPdfView;

namespace Leaflet {

enum class Kind { Browser, EmaPdf, OpenFda };
struct Source {
    QString name;
    QString urlTemplate;      // für Browser-Quellen
    Kind kind = Kind::Browser;
    bool needsPzn = false;
    QString hint;             // Erläuterung für den Tooltip
};

QList<Source> sources();                         // eingebaute + eigene Quellen
QList<Source> customSources();                   // aus den Einstellungen ("Name | URL")
QString customSourcesText();
void setCustomSourcesText(const QString &text);
bool onlineAllowed();                            // Einstellung "im Internet nachschlagen"
void setOnlineAllowed(bool on);

// PZN: 7- oder 8-stellig; Prüfziffer nach PZN-8 (Modulo 11)
QString normalizePzn(const QString &input);      // nur Ziffern, 7 → 8 Stellen; leer bei Unsinn
bool isValidPzn(const QString &pzn8);
QString slug(const QString &text);               // "Ramipril 1A 5 mg" → "ramipril-1a-5-mg"
QUrl expand(const QString &tpl, const Medication &m);
QUrl emaUrl(const Medication &m, const QString &lang);
QUrl openFdaUrl(const Medication &m);
QString openFdaHtml(const QByteArray &json, QString *title = nullptr);  // leer, wenn nichts gefunden
QByteArray htmlToPdf(const QString &html, const QString &title);

constexpr const char *kLeafletDocKey = "leafletDocId";
constexpr const char *kLeafletUrlKey = "leafletUrl";

} // namespace Leaflet

class LeafletDialog : public QDialog
{
    Q_OBJECT
public:
    // patientId 0 = Person noch nicht gespeichert (dann kein Speichern in der Krankenakte)
    LeafletDialog(const Medication &m, qint64 patientId, QWidget *parent = nullptr);
    ~LeafletDialog() override;
    Medication medication() const { return m_med; }   // mit PZN und Verknüpfung
    bool medicationChanged() const { return m_changed; }

    // auch für Tests
    void loadUrl(const QUrl &url, Leaflet::Kind kind = Leaflet::Kind::EmaPdf);
    bool showPdf(const QByteArray &pdf, const QString &origin);
    bool saveToRecord();
    bool busy() const;
    QString statusText() const;
    bool hasContent() const { return !m_pdf.isEmpty(); }

private:
    void openSource(const Leaflet::Source &s);
    void onReply(QNetworkReply *r, Leaflet::Kind kind);
    void showHtml(const QString &html, const QString &origin, const QString &title);
    void showStored();
    void loadFile();
    void askUrl();
    void printPdf();
    void openExternally();
    void updatePzn();
    void setStatus(const QString &html);
    void updateButtons();

    Medication m_med;
    qint64 m_patientId = 0;
    bool m_changed = false;
    QByteArray m_pdf;            // angezeigtes Dokument (PDF)
    QString m_origin;            // Herkunft (URL oder Datei)
    QString m_title;
    bool m_stored = false;       // m_pdf liegt bereits in der Krankenakte
    QNetworkAccessManager *m_net = nullptr;
    QPointer<QNetworkReply> m_reply;

    QLineEdit *m_pzn = nullptr;
    QLabel *m_pznState = nullptr, *m_status = nullptr;
    QStackedWidget *m_stack = nullptr;
    QTextBrowser *m_text = nullptr;
    QPushButton *m_save = nullptr, *m_print = nullptr, *m_external = nullptr;
    QList<QPushButton *> m_onlineButtons;
#ifdef VP_HAVE_QTPDF
    QPdfView *m_view = nullptr;
    QPdfDocument *m_doc = nullptr;
    std::unique_ptr<QTemporaryFile> m_file;
#endif
};
