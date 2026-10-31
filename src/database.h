// VitaParms – Datenhaltung (SQLite über QtSql)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "recurrence.h"

#include "sqlquery.h"

#include <QHash>
#include <QObject>

struct sqlite3;

class Database : public QObject
{
    Q_OBJECT
public:
    static Database &instance();

    // Verschlüsselung: SQLite3 Multiple Ciphers (ChaCha20-Poly1305).
    // 'key' ist der Datenbankschlüssel (zufällig, über das Schlüsselbund
    // der Anmeldekonten freigegeben – siehe keystore.h). Leer = unverschlüsselt.
    enum class OpenResult { Ok, OpenedPlain, NeedKey, WrongKey, Error };
    OpenResult open(const QString &path, const QByteArray &key, QString *error = nullptr);
    bool rekey(const QByteArray &newKey, QString *error = nullptr);   // (de)verschlüsseln/umschlüsseln
    void close();
    bool isOpen() const;
    bool isEncrypted() const { return !m_key.isEmpty(); }
    QString path() const { return m_path; }
    static bool fileHasContent(const QString &path);
    static bool isEncryptedFile(const QString &path);

    // Sicherung als konsistente, ebenso verschlüsselte Kopie (SQLite-Backup-API)
    // withoutFiles: Kopie ohne Dokumentinhalte (für den Abgleich; die Inhalte gehen getrennt)
    bool backupTo(const QString &target, QString *error = nullptr, bool withoutFiles = false);

    // Viele Einzelschreibvorgänge (z. B. Demo-Daten) in einer Transaktion bündeln.
    // Nur um Aufrufe legen, die selbst keine Transaktion öffnen (saveVital, addPreparation).
    bool beginBulk();
    bool commitBulk();

    // Patienten
    QList<Patient> patients();
    std::optional<Patient> patient(qint64 id);
    bool savePatient(Patient &p, QString *error = nullptr);   // insert/update
    bool deletePatient(qint64 id, QString *error = nullptr);

    // Medikation
    QList<Medication> medications(qint64 patientId, bool includeArchived);
    // Medikamente, deren Enddatum vorbei ist, ins Archiv (Historie) verschieben – sie werden nicht
    // mehr gegeben. Ohne Enddatum bleibt ein Medikament aktiv, bis es abgesetzt wird.
    int archiveExpiredMedications();
    static bool expireMedication(Medication &m, const QDate &today = QDate::currentDate());
    bool saveMedications(qint64 patientId, QList<Medication> &meds,
                         const QList<qint64> &deletedIds, QString *error = nullptr);
    // PZN und Verknüpfung zum Beipackzettel (extra) eines gespeicherten Medikaments setzen
    bool updateMedicationLeaflet(qint64 medId, const QString &pzn, const QJsonObject &extra,
                                 QString *error = nullptr);

    // Vitalwerte
    QList<VitalRecord> vitals(qint64 patientId,
                              const QDateTime &from = {}, const QDateTime &to = {},
                              bool includeAdmission = true);
    std::optional<VitalRecord> vital(qint64 id);
    std::optional<VitalRecord> admissionVitals(qint64 patientId);
    std::optional<double> latestHeight(qint64 patientId);
    bool saveVital(VitalRecord &r, QString *error = nullptr);
    bool deleteVital(qint64 id, QString *error = nullptr);

    // Digitale Krankenakte (PDF-Dokumente; Inhalt als BLOB in eigener Tabelle)
    QList<MedicalDocument> documents(qint64 patientId);           // inkl. Volltext
    std::optional<MedicalDocument> document(qint64 id);
    QByteArray documentData(qint64 id);
    bool addDocument(MedicalDocument &d, const QByteArray &pdf, QString *error = nullptr);
    bool updateDocument(const MedicalDocument &d, QString *error = nullptr);  // nur Metadaten
    bool deleteDocument(qint64 id, QString *error = nullptr);
    std::optional<MedicalDocument> findDocumentByHash(qint64 patientId, const QByteArray &sha256);
    QStringList documentCategories(qint64 patientId = 0);
    // Dokumentinhalte für den getrennten Abgleich
    struct DocFileRef { qint64 id = 0; QString uid; QByteArray sha256; bool present = false; };
    QList<DocFileRef> documentFileRefs();
    bool hasDocumentData(qint64 id);
    bool storeDocumentData(qint64 id, const QByteArray &data, QString *error = nullptr);   // ohne Abgleich-Markierung

    // Terminplaner
    QList<Appointment> appointments(const QDateTime &from, const QDateTime &to,
                                    const QString &owner = {}, qint64 patientId = 0,
                                    bool includeDone = true);
    std::optional<Appointment> appointment(qint64 id);
    bool saveAppointment(Appointment &a, QString *error = nullptr);
    bool deleteAppointment(qint64 id, QString *error = nullptr);
    QList<Appointment> pendingReminders(const QString &owner, const QDateTime &now);
    bool markReminded(qint64 id, const QDateTime &when);
    bool snoozeAppointment(qint64 id, const QDateTime &until);
    bool setAppointmentDone(qint64 id, bool done);

    // Terminserien: Die Vorkommen sind normale Termin-Zeilen mit series_id. Unbegrenzte
    // (und weit reichende) Serien werden bis seriesHorizonDays() im Voraus angelegt und
    // von extendSeries() fortgeschrieben.
    std::optional<AppointmentSeries> series(qint64 id);
    QList<AppointmentSeries> allSeries();
    QList<Appointment> seriesAppointments(qint64 seriesId);
    bool createSeries(Appointment &first, Recurrence rule, QString *error = nullptr);
    // Vorkommen ab fromOrig (einschließlich) durch 'first' mit neuer Regel ersetzen
    // (Regel "keine" → Einzeltermin). Für "diesen und alle folgenden" bzw. "alle".
    // carryFirst: Zustand (erledigt/erinnert) des alten Termins am selben Tag übernehmen
    bool splitSeries(qint64 seriesId, const QDateTime &fromOrig, Appointment &first,
                     Recurrence rule, QString *error = nullptr, bool carryFirst = false);
    // Gemeinsame Angaben und Uhrzeit aller Vorkommen ändern (Regel bleibt)
    bool updateSeriesAll(qint64 seriesId, const Appointment &values, QString *error = nullptr);
    bool deleteOccurrence(qint64 appointmentId, QString *error = nullptr);   // merkt EXDATE
    bool truncateSeries(qint64 seriesId, const QDateTime &fromOrig, QString *error = nullptr);
    bool deleteSeries(qint64 seriesId, QString *error = nullptr);
    int  extendSeries();                                    // liefert neu angelegte Vorkommen
    static constexpr int seriesHorizonDays() { return 400; }

    // Protokoll "Medikamente stellen"
    bool addPreparation(MedPreparation &p, QString *error = nullptr);
    bool updatePreparationNote(qint64 id, const QString &note);
    QList<MedPreparation> preparations(qint64 patientId, const QDate &from = {}, const QDate &to = {});
    std::optional<MedPreparation> lastPreparation(qint64 patientId);
    bool deletePreparation(qint64 id, QString *error = nullptr);

    // Eindeutige Kennung dieser Datenbank (z. B. für iCalendar-UIDs)
    QString databaseUuid();

    // Einstellungen in der (verschlüsselten) Datenbank, z. B. SMTP-Zugangsdaten
    QString setting(const QString &key, const QString &fallback = {});
    bool    setSetting(const QString &key, const QString &value);

    // Liefert alle Zusatzfeld-Schlüssel, die in Vitalwerten vorkommen
    QStringList vitalExtraKeys(qint64 patientId);

    // Abgleich zwischen Geräten (siehe dbsync.h)
    struct SyncTable {
        const char *name;
        const char *parentColumn;   // Fremdschlüssel auf patients(id) oder nullptr
        QStringList localColumns;   // gerätebezogene Spalten (nicht abgleichen)
    };
    static const QList<SyncTable> &syncTables();   // in Abhängigkeitsreihenfolge
    static QString syncNow();                      // SQL-Ausdruck: aktuelle Zeit UTC (ms)
    qint64 syncChangeCounter();                    // steigt bei jeder Änderung
    void notifyExternalChange();
    void notifyDocumentsChanged();      // Inhalte nachgeladen (Abgleich)
    void notifyStockChanged(qint64 patientId) { emit stockChanged(patientId); }
    void notifyWoundsChanged(qint64 patientId) { emit woundsChanged(patientId); }                   // nach dem Zusammenführen: Ansichten neu laden
    SqlDb handleDb() { return db(); }
    static constexpr int kSchemaVersion = 12;  // 9: vitals.stool; 10: inr, fluid_in/out, edema; 11: wounds; 12: vitals.pain
    // Datenbankformat der geöffneten bzw. per ATTACH angehängten Datenbank ("r"); -1 = unbekannt
    static int schemaVersionOf(SqlDb &d, const QString &schema = QStringLiteral("main"));
    // Eigenes Formularfeld, das die Schmerzstärke enthielt (bis Version 0.27): "Schmerz (NRS)"
    static bool isPainFieldKey(const QString &key);

signals:
    void patientsChanged();
    void stockChanged(qint64 patientId);                 // Bestand oder Bestellungen geändert
    void vitalsChanged(qint64 patientId);
    void databaseReopened();
    void documentsChanged(qint64 patientId);
    void appointmentsChanged();
    void preparationsChanged(qint64 patientId);
    void woundsChanged(qint64 patientId);                // Wunddokumentation geändert

private:
    Database() = default;
    bool createSchema(QString *error);
    bool ensureColumn(const QString &table, const QString &column, const QString &decl, QString *error);
    // Schema 6: frühere Zusatzfelder "Peak-Flow" (Formular-Eigenschaft vpField) in die Spalte übernehmen
    bool migratePeakFlow(int fromVersion, QString *error);
    bool migratePain(int fromVersion, QString *error);
    // Schema 7: Abgleich zwischen Geräten (uid, sync_at, Löschvermerke, Änderungszähler)
    bool ensureSyncSchema(QString *error);
    bool writeAppointment(Appointment &a, QString *error);
    bool writeSeries(AppointmentSeries &s, QString *error);
    bool generateOccurrences(AppointmentSeries &s, const Appointment &tmpl, const QDate &to,
                             const QHash<QDate, Appointment> &carry, int *created, QString *error);
    bool createSeriesNoTx(Appointment &first, Recurrence rule, const QHash<QDate, Appointment> &carry,
                          QString *error);
    bool cutSeriesNoTx(qint64 seriesId, const QDateTime &fromOrig, QHash<QDate, Appointment> *carry,
                       QString *error);
    QDate horizonFor(const AppointmentSeries &s) const;
    SqlDb db() const;

    sqlite3   *m_db = nullptr;
    QByteArray m_key;
    bool       m_wasOpen = false;
    QString    m_path;
};
