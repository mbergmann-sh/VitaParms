// VitaParms – schlanke Qt-Hülle um die SQLite-C-API (SQLite3 Multiple Ciphers)
//
// Bewusst an QSqlQuery angelehnt (prepare/addBindValue/exec/next/value), damit
// die Datenzugriffe lesbar bleiben. VitaParms verwendet nicht den QSQLITE-Treiber
// von Qt, weil dieser keine Verschlüsselung unterstützt.
// SPDX-License-Identifier: MIT
#pragma once

#include <QHash>
#include <QString>
#include <QVariant>
#include <QVector>

struct sqlite3;
struct sqlite3_stmt;

struct SqlError {
    QString message;
    int     code = 0;
    QString text() const { return message; }
};

class SqlDb
{
public:
    SqlDb() = default;
    explicit SqlDb(sqlite3 *h) : m_h(h) {}
    sqlite3 *handle() const { return m_h; }
    bool isOpen() const { return m_h != nullptr; }

    bool exec(const QString &sql);          // für Anweisungen ohne Ergebnis
    bool transaction() { return exec(QStringLiteral("BEGIN IMMEDIATE")); }
    bool commit()      { return exec(QStringLiteral("COMMIT")); }
    bool rollback()    { return exec(QStringLiteral("ROLLBACK")); }
    SqlError lastError() const;

private:
    sqlite3 *m_h = nullptr;
};

class SqlQuery
{
public:
    explicit SqlQuery(const SqlDb &db) : m_db(db.handle()) {}
    ~SqlQuery() { finish(); }
    SqlQuery(const SqlQuery &) = delete;
    SqlQuery &operator=(const SqlQuery &) = delete;

    bool prepare(const QString &sql);
    void addBindValue(const QVariant &v) { m_binds << v; }
    bool exec();
    bool exec(const QString &sql) { return prepare(sql) && exec(); }
    bool next();
    QVariant value(int col) const;
    QVariant value(const QString &name) const;
    QVariant lastInsertId() const { return m_lastId; }
    SqlError lastError() const { return m_error; }
    void finish();

private:
    void setError();

    sqlite3          *m_db = nullptr;
    sqlite3_stmt     *m_stmt = nullptr;
    QVector<QVariant> m_binds;
    QHash<QString, int> m_cols;
    SqlError          m_error;
    QVariant          m_lastId;
    bool              m_pendingRow = false;   // exec() hat bereits eine Zeile geholt
    bool              m_onRow = false;
    bool              m_done = true;
};
