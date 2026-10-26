// VitaParms – Angaben zu einem Dokument der Krankenakte (Formular: documentform.ui)
// SPDX-License-Identifier: MIT
#pragma once

#include "datatypes.h"
#include <QDialog>

class DocumentDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Import, Edit };

    // 'pdf' wird nur für die Vorschau verwendet (darf leer sein)
    DocumentDialog(const MedicalDocument &doc, const QByteArray &pdf, Mode mode,
                   const QString &progressText = {}, QWidget *parent = nullptr);

    MedicalDocument document() const { return m_doc; }
    bool skipped() const { return m_skipped; }

public slots:
    void accept() override;

private:
    MedicalDocument m_doc;
    QWidget *m_form = nullptr;
    bool m_skipped = false;
};
