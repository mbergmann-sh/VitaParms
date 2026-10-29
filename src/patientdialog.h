// VitaParms – Stammblatt (Formular: patientform.ui)
// SPDX-License-Identifier: MIT
#pragma once

#include "contacts.h"
#include "datatypes.h"
#include <QDialog>

class QTableWidget;

class PatientDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PatientDialog(const Patient &p, QWidget *parent = nullptr);
    Patient patient() const { return m_patient; }
    // für Tests: PDF eines Vorsorgedokuments vormerken (statt Dateiauswahl)
    void setAdvancePdf(int kind, const QByteArray &pdf) { m_advNewPdf[kind] = pdf; m_advUnlink[kind] = false; updateAdvanceUi(); }

public slots:
    void accept() override;

private slots:
    void addMedication();
    void editMedication();
    void archiveMedication();
    void deleteMedication();
    void printMedication();
    void showLeaflet();
    void searchMedications();
    void editAdmission();
    void updateAmputationInfo();
    void setupPeakFlow();
    void setupTargets();          // INR-Zielbereich, Trinkmenge, Gewichtskontrolle
    void updatePeakFlowInfo();
    void updateBioSex(bool fromSexField);
    void setupCareWidgets();
    void setupAdvanceWidgets();
    void loadAdvance(const Patient &p);
    void updateAdvanceUi();
    bool storeAdvancePdfs(Patient &p, QString *error);   // nach dem Speichern der Person (id bekannt)
    void advancePdfMenu(int kind);
    void updateCareEnabled();
    void updateMedicationButtons();
    // Kontakte (Weitere Angaben)
    void setupContacts();
    void fillContactTable();
    void addContact();
    void editContact();
    void deleteContact();

private:
    void fillMedicationTable();
    QStringList medicationCells(const Medication &m) const;   // Anzeigetexte je Spalte
    void sortMedications(int column);                         // Klick auf Spaltenkopf
    int  currentMedicationIndex() const;
    Patient collectPatient() const;
    void updateAdmissionLabel();
    void setupWoundWidgets();        // „Wunden bei Aufnahme“ unter den Aufnahmewerten
    void updateWoundLabel();
    void addAdmissionWound();

    Patient  m_patient;
    bool     m_bioSexTouched = false;   // biologisches Geschlecht von Hand gewählt
    // Vorsorgedokumente (Patientenverfügung, Vorsorge-, Betreuungsvollmacht)
    class QCheckBox *m_advCheck[3] = {};
    class QLineEdit *m_advPerson[3] = {}, *m_advAddr[3] = {}, *m_advPhone[3] = {};
    class QDateEdit *m_advValid[3] = {};
    class QToolButton *m_advPdf[3] = {};
    QByteArray m_advNewPdf[3];         // neu gewählte PDF (wird beim Speichern in die Krankenakte gelegt)
    bool m_advUnlink[3] = {};          // Verknüpfung entfernen
    QWidget *m_form = nullptr;
    QTableWidget *m_medTable = nullptr;
    QTableWidget *m_contactTable = nullptr;
    QList<Contacts::Contact> m_contacts;

    QList<Medication> m_meds;          // alle (inkl. archivierte)
    int m_sortColumn = -1;             // -1 = Reihenfolge der Eingabe
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    QList<qint64>     m_deletedMedIds;
    QList<Medication> m_removedMeds;     // gelöschte (für das Beenden ihrer Terminserien)
    std::optional<VitalRecord> m_admission;
    bool m_admissionChanged = false;
    class QLabel *m_woundLabel = nullptr;
    class QPushButton *m_woundAdd = nullptr, *m_woundOpen = nullptr;
};
