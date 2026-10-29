// SPDX-License-Identifier: MIT
#include "sqlquery.h"
#include "qtcompat.h"

#include "sqlite3mc.h"

bool SqlDb::exec(const QString &sql)
{
    if (!m_h) return false;
    char *err = nullptr;
    const int rc = sqlite3_exec(m_h, sql.toUtf8().constData(), nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

SqlError SqlDb::lastError() const
{
    if (!m_h) return { QStringLiteral("Datenbank nicht geöffnet"), SQLITE_MISUSE };
    return { QString::fromUtf8(sqlite3_errmsg(m_h)), sqlite3_extended_errcode(m_h) };
}

void SqlQuery::finish()
{
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
        m_stmt = nullptr;
    }
    m_pendingRow = m_onRow = false;
    m_done = true;
}

void SqlQuery::setError()
{
    m_error = m_db ? SqlError{ QString::fromUtf8(sqlite3_errmsg(m_db)), sqlite3_extended_errcode(m_db) }
                   : SqlError{ QStringLiteral("Datenbank nicht geöffnet"), SQLITE_MISUSE };
}

bool SqlQuery::prepare(const QString &sql)
{
    finish();
    m_binds.clear();
    m_cols.clear();
    m_error = {};
    if (!m_db) { setError(); return false; }
    const QByteArray s = sql.toUtf8();
    if (sqlite3_prepare_v2(m_db, s.constData(), int(s.size()), &m_stmt, nullptr) != SQLITE_OK) {
        setError();
        m_stmt = nullptr;
        return false;
    }
    const int n = sqlite3_column_count(m_stmt);
    for (int i = 0; i < n; ++i)
        m_cols.insert(QString::fromUtf8(sqlite3_column_name(m_stmt, i)), i);
    return true;
}

bool SqlQuery::exec()
{
    if (!m_stmt) { if (m_error.message.isEmpty()) setError(); return false; }
    sqlite3_reset(m_stmt);
    sqlite3_clear_bindings(m_stmt);
    for (int i = 0; i < m_binds.size(); ++i) {
        const QVariant &v = m_binds[i];
        const int idx = i + 1;
        int rc = SQLITE_OK;
        if (!v.isValid() || vpIsNull(v)) {
            rc = sqlite3_bind_null(m_stmt, idx);
        } else {
            switch (vpTypeId(v)) {
            case QMetaType::QByteArray: {
                const QByteArray b = v.toByteArray();
                rc = sqlite3_bind_blob64(m_stmt, idx, b.constData(), sqlite3_uint64(b.size()), SQLITE_TRANSIENT);
                break;
            }
            case QMetaType::Bool:
            case QMetaType::Int:
            case QMetaType::UInt:
            case QMetaType::LongLong:
            case QMetaType::ULongLong:
                rc = sqlite3_bind_int64(m_stmt, idx, v.toLongLong());
                break;
            case QMetaType::Double:
            case QMetaType::Float:
                rc = sqlite3_bind_double(m_stmt, idx, v.toDouble());
                break;
            default: {
                const QByteArray t = v.toString().toUtf8();
                rc = sqlite3_bind_text64(m_stmt, idx, t.constData(), sqlite3_uint64(t.size()),
                                         SQLITE_TRANSIENT, SQLITE_UTF8);
            }
            }
        }
        if (rc != SQLITE_OK) { setError(); return false; }
    }
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) {
        m_pendingRow = true;
        m_done = false;
    } else if (rc == SQLITE_DONE) {
        m_pendingRow = false;
        m_done = true;
    } else {
        setError();
        sqlite3_reset(m_stmt);
        return false;
    }
    m_lastId = qlonglong(sqlite3_last_insert_rowid(m_db));
    m_onRow = false;
    return true;
}

bool SqlQuery::next()
{
    if (!m_stmt) return false;
    if (m_pendingRow) {
        m_pendingRow = false;
        m_onRow = true;
        return true;
    }
    if (m_done) { m_onRow = false; return false; }
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW) { m_onRow = true; return true; }
    if (rc != SQLITE_DONE) setError();
    m_done = true;
    m_onRow = false;
    return false;
}

QVariant SqlQuery::value(int col) const
{
    if (!m_stmt || !m_onRow || col < 0 || col >= sqlite3_column_count(m_stmt)) return {};
    switch (sqlite3_column_type(m_stmt, col)) {
    case SQLITE_INTEGER: return qlonglong(sqlite3_column_int64(m_stmt, col));
    case SQLITE_FLOAT:   return sqlite3_column_double(m_stmt, col);
    case SQLITE_TEXT:
        return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, col)),
                                 sqlite3_column_bytes(m_stmt, col));
    case SQLITE_BLOB: {
        const void *p = sqlite3_column_blob(m_stmt, col);
        return QByteArray(static_cast<const char *>(p), sqlite3_column_bytes(m_stmt, col));
    }
    default:
        return QVariant();   // NULL
    }
}

QVariant SqlQuery::value(const QString &name) const
{
    const auto it = m_cols.constFind(name);
    return it == m_cols.constEnd() ? QVariant() : value(it.value());
}
