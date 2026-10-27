// VitaParms – Assistent zum Stellen von Medikamenten mit Sprachausgabe
// Liest die aktuelle Medikamentenliste Schritt für Schritt vor:
// "Weiter" → nächstes Medikament, "Wiederholen" → nochmals vorlesen.
// Am Ende: optischer und akustischer Hinweis "Medikamente sind gerichtet".
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QDialog>
#include <QSet>

class QCheckBox;
class QLineEdit;
class QComboBox;
class QDateEdit;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class Speech;

class MedicationAssistant : public QDialog
{
    Q_OBJECT
public:
    QStringList stockShortage() const { return m_shortage; }   // nach dem Stellen: Bestand reichte nicht
    MedicationAssistant(const Patient &p, const QList<Medication> &meds, QWidget *parent = nullptr);

    static QList<Medication> currentMedications(const QList<Medication> &all, const QDate &day);
    // Text, der für ein Medikament vorgelesen wird (slot: -1 = alle Zeitpunkte, 0..3)
    static QString speechText(const Medication &m, int slot);
    int index() const { return m_index; }
    bool finishedAll() const { return m_finished; }
    qint64 loggedId() const { return m_prepId; }
    void setDay(const QDate &d);
    // Tagesdosierer (1 Tag) oder Wochendosierer (7 Tage ab dem gewählten Tag)
    void setWeekly(bool weekly);
    int days() const;
    // Vorlesetext für den Wochendosierer: Tage mit gleicher Dosis zusammengefasst, dazu die Gesamtmenge
    static QString weekSpeechText(const Medication &m, const QDate &start, int days, int slot);
    int count() const { return int(m_list.size()); }
    static QString doseSummary(const Medication &m, int slot);   // "morgens: 1 Stück, abends: 1 Stück"

public slots:
    void done(int r) override;

    void next();
    void repeat();
    void previous();
    void restart();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void applyFilter();
    QString textFor(const Medication &m) const;
    bool dueInRange(const Medication &m, int slot) const;
    void showMedication(int i, bool speak);
    void showFinished();
    void speak(const QString &text, bool force = false);
    void updateVoiceInfo();
    void updateLastLabel();
    void logPreparation(bool complete);
    void saveNote();

    Patient m_patient;
    QList<Medication> m_source, m_list;   // alle / vorzulesen
    int  m_index = -1;
    bool m_finished = false;
    QStringList m_shortage;       // Bestand reichte beim Stellen nicht
    QSet<int> m_confirmed;        // mit "Weiter" bestätigte Medikamente
    qint64 m_prepId = 0;          // Protokolleintrag dieses Durchgangs
    QLineEdit *m_note = nullptr;
    QLabel *m_lastLabel = nullptr, *m_loggedLabel = nullptr;
    Speech *m_speech = nullptr;

    QComboBox *m_slot = nullptr, *m_voice = nullptr, *m_mode = nullptr;
    QLabel *m_forLabel = nullptr, *m_rangeLabel = nullptr;
    QDateEdit *m_date = nullptr;
    QLabel *m_dayInfo = nullptr;
    QCheckBox *m_autoRead = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_progress = nullptr, *m_name = nullptr, *m_details = nullptr, *m_hint = nullptr,
           *m_voiceInfo = nullptr, *m_doneLabel = nullptr;
    QTableWidget *m_doses = nullptr;
    QPushButton *m_prev = nullptr, *m_repeat = nullptr, *m_next = nullptr, *m_leafletBtn = nullptr;
};
