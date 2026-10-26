// VitaParms – Bestandsführung für Medikamente ("Warenwirtschaft")
//
//  * Bestand je Packung mit Stückzahl und Verfallsdatum; verbraucht wird die zuerst
//    ablaufende Packung (FEFO).
//  * Nach jedem Stellvorgang (Assistent „Medikamente stellen“) wird die gestellte Menge
//    abgebucht – je Medikament, Tag und Einnahmezeit höchstens einmal.
//  * Reichweite: Tag für Tag nach Dosierschema und Einnahme-Rhythmus vorausgerechnet;
//    verfallene Packungen zählen nicht. Bestellen bis = Reichweite − Vorlauf; fällt das auf
//    ein Wochenende, gilt der Freitag davor (am Wochenende kann nicht bestellt werden).
//  * Bestellliste: offene Bestellung (Erinnerung, bis sie als „bestellt“ bestätigt ist) →
//    bestellt → geliefert (Lieferung bucht eine neue Packung ein).
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>

#include <optional>

namespace MedStock {

struct Package {
    qint64  id = 0;
    qint64  patientId = 0;
    qint64  medicationId = 0;
    double  quantity = 0;
    QDate   expiry;          // ungültig = ohne Verfallsdatum
    QDate   received;
    QString note;
    bool expiredOn(const QDate &d) const { return expiry.isValid() && expiry < d; }
};

enum MoveKind { Preparation = 0, Receipt, Correction, Discard };

struct Move {
    qint64    id = 0;
    qint64    medicationId = 0;
    qint64    stockId = 0;
    QDateTime at;
    QDate     forDate;
    int       kind = Preparation;
    double    quantity = 0;   // + Zugang, − Abgang
    QString   by;
    QString   note;
};

enum OrderStatus { Open = 0, Ordered, Delivered };

struct OrderItem {
    qint64  id = 0;
    qint64  orderId = 0;
    qint64  medicationId = 0;
    QString name, substance, strength, form, pzn;
    QString amount;           // z. B. „1 Packung (N3, 100 Stück)“
    QString reason;           // z. B. „reicht bis Do 29.10.“
    bool    delivered = false;
    double  deliveredQty = 0;
    QDate   deliveredAt;
};

struct Order {
    qint64    id = 0;
    qint64    patientId = 0;
    QDateTime createdAt;
    QString   createdBy;
    int       status = Open;
    QDateTime orderedAt;
    QString   orderedBy;
    QDate     deliveredAt;
    QString   note;
    QList<OrderItem> items;
    QString statusText() const;
};

enum State { NotTracked = 0, Ok, OrderSoon, OrderNow, Empty };

struct Forecast {
    bool    tracked = false;
    double  stock = 0;        // verwendbar (nicht verfallen)
    double  expiredQty = 0;   // verfallen, noch nicht entsorgt
    double  daily = 0;        // Durchschnitt der nächsten 28 Tage
    QDate   runOut;           // erster Tag, an dem der Bestand nicht mehr reicht (ungültig: reicht > 1 Jahr)
    QDate   orderBy;          // spätester Bestelltag (Werktag)
    QDate   nextExpiry;       // nächstes Verfallsdatum einer verwendbaren Packung
    int     state = NotTracked;
    bool    onOrder = false;  // in einer offenen oder bestellten, nicht gelieferten Bestellung
    bool    orderConfirmed = false;   // … und diese Bestellung ist als bestellt bestätigt
    QString reason(const QString &unit) const;   // Begründung für die Bestellliste
    QString stateText() const;
};

// --- Stell-Rhythmus der Person: täglich (Tagesdosierer) oder wöchentlich (Wochendosierer, ab Wochentag)
struct PrepPlan {
    int days = 1;        // 1 oder 7
    int weekday = 1;     // wöchentlich: erster Tag der Woche, die gestellt wird (1 = Montag)
    bool weekly() const { return days >= 7; }
};
PrepPlan prepPlan(qint64 patientId);
void setPrepPlan(qint64 patientId, const PrepPlan &plan);
QDate nextPrepStart(const PrepPlan &plan, const QDate &from);   // nächster Starttag ab 'from' (einschließlich)
QString prepPlanText(const PrepPlan &plan);                      // "täglich" / "wöchentlich ab Montag"

// --- Einstellungen
int  leadDays();               // Vorlauf in Tagen (Rezept besorgen + Lieferung), Standard 7
void setLeadDays(int days);

// --- Hilfen
double parseAmount(const QString &text);                       // "1", "½", "1/2", "1,5", "1½" → Zahl
double doseOn(const Medication &m, const QDate &day, int slot = -1);   // gestellte Menge an diesem Tag
bool   isTracked(const Medication &m);
QDate  orderDeadline(const QDate &runOut, int lead);           // Wochenende → Freitag davor
QString qtyText(double q);                                      // 12 / 1,5

// --- Daten
QList<Package> packages(qint64 medicationId, bool includeEmpty = false);
bool addPackage(const Medication &m, Package &p, const QString &note = {}, QString *error = nullptr);
bool changePackage(const Package &p, double newQty, const QDate &expiry, const QString &note, QString *error = nullptr);
bool discardPackage(const Package &p, QString *error = nullptr);
QList<Move> moves(qint64 medicationId, int limit = 200);
void setTracked(Medication &m, bool on);                       // speichert die Kennzeichnung

// Stellvorgang abbuchen (je Medikament, Tag und Einnahmezeit nur einmal). Liefert die Namen
// der Medikamente, bei denen der Bestand nicht reichte.
QStringList bookPreparation(qint64 patientId, const QList<Medication> &meds, const QDate &day, int slot);

Forecast forecast(const Medication &m, const QDate &today = QDate::currentDate());

// --- Bestellungen
QList<Order> orders(qint64 patientId);
std::optional<Order> order(qint64 orderId);
// Neu fällige Medikamente in die offene Bestellung übernehmen (anlegen, falls nötig).
// Liefert die offene Bestellung (id 0, wenn nichts zu bestellen ist).
Order updateOpenOrder(qint64 patientId);
bool addToOpenOrder(qint64 patientId, const Medication &m, const QString &reason, QString *error = nullptr);
bool confirmOrdered(qint64 orderId, const QDateTime &at, const QString &by, const QString &note, QString *error = nullptr);
bool deliverItem(const OrderItem &item, double qty, const QDate &expiry, QString *error = nullptr);
bool updateItemAmount(qint64 itemId, const QString &amount);
bool removeItem(qint64 itemId);
bool deleteOrder(qint64 orderId);
// Hinweise für die Erinnerung: offene (nicht bestätigte) Positionen
QStringList reminderLines(const Order &open);

} // namespace MedStock
