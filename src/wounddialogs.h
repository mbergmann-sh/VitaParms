// VitaParms – Wunddokumentation am Computer
//   WoundsDialog      Wunden einer Person mit Chronik, Flächenverlauf, Foto, Druck/PDF/Excel
//   WoundDialog       Wunde hinzufügen / bearbeiten (Wundanamnese, auch „bei Aufnahme vorhanden“)
//   WoundEntryDialog  Wundbeurteilung bzw. Verbandwechsel erfassen / bearbeiten
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include "wounds.h"

#include <QDialog>
#include <QMap>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;

// Mehrfachauswahl als Häkchen in Spalten
class ChoiceGroup : public QWidget
{
    Q_OBJECT
public:
    ChoiceGroup(const QStringList &choices, int columns, QWidget *parent = nullptr);
    QStringList values() const;
    void setValues(const QStringList &values);
private:
    QList<QCheckBox *> m_boxes;
    QStringList m_extra;           // Werte, die nicht in der Auswahl stehen (bleiben erhalten)
};

class WoundDialog : public QDialog
{
    Q_OBJECT
public:
    WoundDialog(const Wounds::Wound &w, QWidget *parent = nullptr);
    Wounds::Wound wound() const;
    void accept() override;
protected:
    void showEvent(QShowEvent *e) override;
private:
    void updateGrades();
    Wounds::Wound m_wound;
    QLineEdit *m_label = nullptr, *m_diagnosis = nullptr, *m_siteDetail = nullptr, *m_recFree = nullptr, *m_physician = nullptr;
    QComboBox *m_kind = nullptr, *m_grade = nullptr, *m_site = nullptr, *m_side = nullptr, *m_origin = nullptr,
              *m_goal = nullptr, *m_status = nullptr;
    QLabel *m_gradeLabel = nullptr;
    QCheckBox *m_sinceKnown = nullptr, *m_onAdmission = nullptr, *m_consent = nullptr;
    QDateEdit *m_since = nullptr, *m_admission = nullptr, *m_closed = nullptr;
    QSpinBox *m_recurrences = nullptr;
    QPlainTextEdit *m_previous = nullptr, *m_self = nullptr, *m_notes = nullptr;
    ChoiceGroup *m_limits = nullptr;
};

class WoundEntryDialog : public QDialog
{
    Q_OBJECT
public:
    WoundEntryDialog(const Wounds::Wound &w, const Wounds::Entry &e, QWidget *parent = nullptr);
    Wounds::Entry entry() const;
    void accept() override;
    void setPhotoData(const QByteArray &jpeg);      // für Tests (statt Dateiauswahl)
protected:
    void showEvent(QShowEvent *e) override;
private:
    void updateKind();
    void updateArea();
    void updatePhoto();
    Wounds::Wound m_wound;
    Wounds::Entry m_entry;
    QComboBox *m_kind = nullptr, *m_exudate = nullptr, *m_exudateKind = nullptr, *m_odor = nullptr, *m_grade = nullptr,
              *m_phase = nullptr, *m_trend = nullptr;
    QDateTimeEdit *m_at = nullptr;
    QDoubleSpinBox *m_len = nullptr, *m_wid = nullptr, *m_dep = nullptr;
    QLabel *m_area = nullptr, *m_groundSum = nullptr, *m_photo = nullptr, *m_photoHint = nullptr;
    QLineEdit *m_pockets = nullptr, *m_description = nullptr, *m_cleansing = nullptr, *m_dressing = nullptr,
              *m_fixation = nullptr, *m_compression = nullptr;
    QSpinBox *m_gran = nullptr, *m_epi = nullptr, *m_fib = nullptr, *m_nec = nullptr, *m_pain = nullptr, *m_painChange = nullptr,
             *m_interval = nullptr;
    QCheckBox *m_hasNext = nullptr;
    QDateEdit *m_next = nullptr;
    ChoiceGroup *m_exposed = nullptr, *m_edge = nullptr, *m_surround = nullptr, *m_infection = nullptr;
    QPlainTextEdit *m_notes = nullptr;
    QPushButton *m_photoBtn = nullptr, *m_photoDel = nullptr;
    QList<QWidget *> m_assessOnly;      // nur bei der vollständigen Wundbeurteilung
};

class WoundsDialog : public QDialog
{
    Q_OBJECT
public:
    WoundsDialog(const Patient &p, QWidget *parent = nullptr, qint64 selectWound = 0);
    void selectWound(qint64 id);
    qint64 currentWoundId() const;
    // für Tests
    QTableWidget *chronicle() const { return m_table; }
private:
    void reload();
    void showWound();
    void showEntry();
    void addWound();
    void editWound();
    void deleteWound();
    void addEntry(int kind);
    void editEntry();
    void deleteEntry();
    void markHealed();
    void output(int how);          // 0 Drucken, 1 Vorschau, 2 PDF, 3 Excel
    Patient m_patient;
    QList<Wounds::Wound> m_wounds;
    QList<Wounds::Entry> m_entries;
    QListWidget *m_list = nullptr;
    QCheckBox *m_showClosed = nullptr;
    QLabel *m_head = nullptr, *m_chart = nullptr, *m_photo = nullptr, *m_entryText = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_btnEdit = nullptr, *m_btnDelete = nullptr, *m_btnAssess = nullptr, *m_btnDressing = nullptr,
                *m_btnEditEntry = nullptr, *m_btnDelEntry = nullptr, *m_btnHealed = nullptr, *m_btnOut = nullptr;
};
