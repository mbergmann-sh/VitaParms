// VitaParms – Reiter "Dokumente": digitale Krankenakte je Benutzer
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"

#include <QAbstractTableModel>
#include <QWidget>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTemporaryFile;
class QPdfDocument;
class QPdfView;

class DocumentModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Col { ColDate, ColCategory, ColTitle, ColIssuer, ColPages, ColText, ColHit, ColCount };
    using QAbstractTableModel::QAbstractTableModel;

    void setDocuments(const QList<MedicalDocument> &docs, const QStringList &terms);
    const MedicalDocument *at(int row) const;
    const QList<MedicalDocument> &documents() const { return m_docs; }

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    QList<MedicalDocument> m_docs;
    QStringList m_terms;
};

class DocumentsPage : public QWidget
{
    Q_OBJECT
public:
    explicit DocumentsPage(QWidget *parent = nullptr);
    ~DocumentsPage() override;

    void setPatient(const std::optional<Patient> &p);

    // Für Menüaktionen und Tests
    void importFiles(const QStringList &files);
    QList<qint64> selectedIds() const;
    QList<qint64> filteredIds() const;
    QList<qint64> allIds() const;
    DocumentModel *model() const { return m_model; }
    void setSearchText(const QString &t);
    static void cleanupTempFiles();   // beim Programmende: Ansichtskopien löschen

public slots:
    void addDocuments();
    void editDocument();
    void deleteDocuments();
    void openExternal();
    void saveCopies();
    void printSelected();
    void printList();
    void packAndSend();
    void reload();

signals:
    void selectionChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private slots:
    void applyFilter();
    void updatePreview();
    void updateButtons();

private:
    QList<MedicalDocument> selectedDocs() const;

    std::optional<Patient> m_patient;
    QList<MedicalDocument> m_all;

    QLineEdit  *m_search = nullptr;
    QComboBox  *m_category = nullptr;
    QComboBox  *m_year = nullptr;
    QLabel     *m_count = nullptr;
    QTableView *m_table = nullptr;
    DocumentModel *m_model = nullptr;
    QSplitter  *m_splitter = nullptr;
    QStackedWidget *m_previewStack = nullptr;
    QLabel     *m_previewInfo = nullptr;
    QLabel     *m_noPreview = nullptr;
    QList<QWidget *> m_needSel;
    QList<QWidget *> m_needOne;
    qint64      m_previewId = 0;
#ifdef VP_HAVE_QTPDF
    QPdfDocument *m_pdf = nullptr;
    QPdfView *m_view = nullptr;
    std::unique_ptr<QTemporaryFile> m_previewFile;
#endif
};
