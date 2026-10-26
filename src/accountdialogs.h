// VitaParms – Dialoge für Anmeldung und Anmeldekonten
// SPDX-License-Identifier: MIT
#pragma once

#include "keystore.h"

#include <QDialog>
#include <QMessageBox>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

// Anmeldung an einer Datenbank
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    LoginDialog(const QString &dbPath, const KeyStore &ks, const QString &presetUser, QWidget *parent = nullptr);
    QString user() const { return m_userName; }
    Role role() const { return m_role; }
    QByteArray dbKey() const { return m_key; }
    bool recoveryRequested() const { return m_recovery; }
    bool importRequested() const { return m_import; }

public slots:
    void accept() override;

private:
    const KeyStore &m_ks;
    QLineEdit *m_name = nullptr, *m_pw = nullptr;
    QLabel *m_msg = nullptr;
    QString m_userName;
    Role m_role = Role::User;
    QByteArray m_key;
    bool m_recovery = false;
    bool m_import = false;
    int m_failures = 0;
};

// Neues Konto bzw. erstes Administrator-Konto
class AccountEditDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { FirstAdmin, NewAccount, ResetPassword, ChangeOwnPassword };
    AccountEditDialog(Mode mode, const QString &intro, const QString &presetName = {}, QWidget *parent = nullptr);
    QString name() const;
    QString password() const;
    QString oldPassword() const;
    Role role() const;

public slots:
    void accept() override;

private:
    Mode m_mode;
    QLineEdit *m_name = nullptr, *m_old = nullptr, *m_pw1 = nullptr, *m_pw2 = nullptr;
    QComboBox *m_role = nullptr;
};

// Anzeige des Wiederherstellungsschlüssels (Drucken/Speichern/Kopieren)
class RecoveryCodeDialog : public QDialog
{
    Q_OBJECT
public:
    RecoveryCodeDialog(const QString &code, const QString &dbPath, bool requireConfirm, QWidget *parent = nullptr);

private:
    void print();
    void saveFile();
    QString m_code, m_dbPath;
};

// Eingabe des Wiederherstellungsschlüssels
QByteArray askRecoveryKey(QWidget *parent, const QString &reason);

// Kontenverwaltung (Administratoren) bzw. eigenes Passwort ändern (Anwender)
class AccountsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AccountsDialog(QWidget *parent = nullptr);

private:
    void refresh();
    QString selectedName() const;
    bool persist();
    void addAccount();
    void resetPassword();
    void toggleRole();
    void removeAccount();
    void changeOwnPassword();
    void showRecovery();

    QTableWidget *m_table = nullptr;
    QList<QWidget *> m_adminOnly;
};

// Allererster Start: Sprache wählen (zweisprachig, ohne Übersetzung)
class LanguageDialog : public QDialog
{
    Q_OBJECT
public:
    explicit LanguageDialog(QWidget *parent = nullptr);
    QString chosen() const { return m_chosen; }   // "de"/"en", leer = geschlossen
private:
    QString m_chosen;
};

// Erster Start ohne Datenbank: neu anlegen, importieren, Erste Schritte, Demo
class FirstStartBox : public QMessageBox
{
    Q_OBJECT
public:
    explicit FirstStartBox(QWidget *parent = nullptr);
    QPushButton *freshButton() const { return m_fresh; }
    QPushButton *importButton() const { return m_import; }
private:
    QPushButton *m_fresh = nullptr, *m_import = nullptr;
};

