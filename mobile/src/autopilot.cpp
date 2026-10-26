// VitaParms Tablet – automatischer Rundgang für Bildschirmfotos und Tests
// Nur im Testbau (qmake CONFIG+=autopilot); nicht Teil der ausgelieferten App.
//   VP_AUTOPILOT_DB   Datenbank (mit .keys), VP_AUTOPILOT_USER / _PASSWORD, VP_AUTOPILOT_OUT Zielordner
// SPDX-License-Identifier: MIT
#include "appcontroller.h"
#include "dbtransfer.h"
#include "usbsync.h"
#include "peakflow.h"
#include "database.h"
#include <QUrl>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QImage>
#include <QPainter>
#include <QCryptographicHash>
#include "pdftools.h"
#include "sqlquery.h"
#include <QQmlApplicationEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QDate>
#include <functional>

static void wait(int ms)
{
    QEventLoop l;
    QTimer::singleShot(ms, &l, &QEventLoop::quit);
    l.exec();
}

int runAutopilot(QQmlApplicationEngine &engine, AppController &app)
{
    const QString out = qEnvironmentVariable("VP_AUTOPILOT_OUT");
    QDir().mkpath(out);
    auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!win) return 2;
    int fails = 0;
    qint64 m_testMed = 0;
    qint64 m_testSeries = 0, m_testAppt = 0;
    auto shot = [&](const QString &name) {
        wait(450);
        const QImage img = win->grabWindow();
        if (img.isNull()) { ++fails; qWarning("Bild leer: %s", qPrintable(name)); return; }
        img.save(out + QLatin1Char('/') + name + QStringLiteral(".png"));
    };
    auto home = [&]() { return win->findChild<QObject *>(QStringLiteral("home")); };
    auto stackPop = [&]() {
        QMetaObject::invokeMethod(win, "popPage");
        wait(300);
    };
    auto call = [&](QObject *o, const char *fn, const QVariant &arg = {}) {
        if (!o) { ++fails; return; }
        if (arg.isValid()) QMetaObject::invokeMethod(o, fn, Q_ARG(QVariant, arg));
        else QMetaObject::invokeMethod(o, fn);
    };

    if (!app.loggedIn()) {          // ohne Datenbank: Einrichtung zeigen
        shot(QStringLiteral("m50_einrichtung"));
        return fails;
    }
    // ---- Funktionsprüfung der Brücke (auf der Kopie der Demo-Datenbank)
    auto check = [&](bool ok, const char *what) { if (!ok) { ++fails; qWarning("FEHLER: %s", what); } };
    const QVariantList ps = app.persons();
    check(ps.size() >= 1, "Personen");
    const qint64 pid = ps.value(0).toMap().value(QStringLiteral("id")).toLongLong();
    {
        QVariantMap v = app.newVital(pid);
        QVariantMap vals{ { QStringLiteral("systolic"), 128 }, { QStringLiteral("diastolic"), 82 }, { QStringLiteral("pulse"), 71 },
                          { QStringLiteral("glucose"), QStringLiteral("104") }, { QStringLiteral("weight"), QStringLiteral("77,5") },
                          { QStringLiteral("Atemfrequenz"), 16 }, { QStringLiteral("Schmerz (NRS)"), 2 } };
        v.insert(QStringLiteral("values"), vals);
        v.insert(QStringLiteral("context"), QStringLiteral("nüchtern"));
        v.insert(QStringLiteral("observations"), QStringLiteral("Tablet-Test"));
        check(app.checkVital(v).isEmpty(), "Plausibilität");
        const qint64 id = app.saveVital(pid, v);
        check(id > 0, "Messung speichern");
        const QVariantMap back = app.vital(id);
        const QVariantMap bv = back.value(QStringLiteral("values")).toMap();
        check(bv.value(QStringLiteral("weight")).toDouble() == 77.5 && bv.value(QStringLiteral("Atemfrequenz")).toInt() == 16
              && bv.value(QStringLiteral("Schmerz (NRS)")).toInt() == 2, "Messung zurücklesen");
        QVariantMap bad = v; bad.insert(QStringLiteral("values"), QVariantMap{ { QStringLiteral("systolic"), 70 }, { QStringLiteral("diastolic"), 90 } });
        check(!app.checkVital(bad).isEmpty(), "Plausibilität vertauscht");
        check(app.journal(pid, QStringLiteral("Tablet-Test")).size() == 1, "Suche im Journal");
        check(app.deleteVital(id) && app.journal(pid, QStringLiteral("Tablet-Test")).isEmpty(), "Messung löschen");
        check(!app.bmiPreview(pid, 77.5, QVariant()).isEmpty(), "BMI-Vorschau");
    }
    {
        QVariantMap p = app.personDetail(pid);
        p.insert(QStringLiteral("careLevel"), 3);
        p.insert(QStringLiteral("careReview"), QStringLiteral("2027-03-01"));
        p.insert(QStringLiteral("disabilityDegree"), 70);
        p.insert(QStringLiteral("disabilityMarks"), QStringList{ QStringLiteral("G"), QStringLiteral("B") });
        p.insert(QStringLiteral("disabilityValid"), QString());
        check(app.savePerson(p) == pid, "Stammblatt speichern");
        const QVariantMap s = app.personSummary(pid);
        check(s.value(QStringLiteral("careLevel")).toInt() == 3 && s.value(QStringLiteral("disabilityText")).toString().contains(QStringLiteral("G, B")), "Pflegegrad/Ausweis");
        QVariantMap np{ { QStringLiteral("lastName"), QStringLiteral("Testperson") } };
        const qint64 nid = app.savePerson(np);
        check(nid > 0, "Neue Person");
        check(app.deletePerson(nid), "Person löschen");
    }
    {
        QVariantMap m = app.medication(pid, 0);
        m.insert(QStringLiteral("name"), QStringLiteral("Testomed")); m.insert(QStringLiteral("morning"), QStringLiteral("1"));
        check(app.saveMedication(pid, m), "Medikament speichern");
        qint64 mid = 0;
        for (const QVariant &x : app.medications(pid, false)) if (x.toMap().value(QStringLiteral("name")) == QStringLiteral("Testomed")) mid = x.toMap().value(QStringLiteral("id")).toLongLong();
        check(mid > 0, "Medikament gefunden");
        // An Einnahme erinnern: Tage und Uhrzeiten
        QVariantMap r = app.medication(pid, mid);
        check(!r.value(QStringLiteral("remind")).toBool() && r.value(QStringLiteral("remindSlots")).toList().size() == 4, "Erinnerung aus");
        r.insert(QStringLiteral("remind"), true);
        r.insert(QStringLiteral("remindDays"), QVariantList{ true, true, true, true, true, false, false });
        r.insert(QStringLiteral("remindTimes"), QVariantList{ QStringLiteral("7:30"), QStringLiteral("19:00") });
        check(app.saveMedication(pid, r), "Erinnerung speichern");
        const QVariantMap r2 = app.medication(pid, mid);
        qInfo("Erinnerung: %s", qPrintable(r2.value(QStringLiteral("reminderText")).toString()));
        check(r2.value(QStringLiteral("remind")).toBool() && r2.value(QStringLiteral("reminderText")).toString().contains(QStringLiteral("07:30")), "Erinnerung zurücklesen");
        int pills = 0;
        for (const QVariant &a : app.upcoming(pid, 4)) pills += a.toMap().value(QStringLiteral("title")).toString().contains(QStringLiteral("Testomed"));
        check(pills >= 1, "Erinnerung in der Terminliste");
        QVariantMap bad = r2; bad.insert(QStringLiteral("remindTimes"), QVariantList{ QStringLiteral("25:99") });
        check(!app.saveMedication(pid, bad), "Uhrzeit prüfen");
        // Absetzen mit Grund, dann wieder aufnehmen
        QVariantMap st = app.medication(pid, mid);
        check(st.value(QStringLiteral("endDate")).toString().isEmpty(), "Standard: kein Enddatum");
        st.insert(QStringLiteral("archived"), true); st.insert(QStringLiteral("stopReason"), QStringLiteral("Nebenwirkung"));
        check(app.saveMedication(pid, st), "Absetzen speichern");
        const QVariantMap st2 = app.medication(pid, mid);
        check(st2.value(QStringLiteral("archived")).toBool() && st2.value(QStringLiteral("endDate")).toString() == QDate::currentDate().toString(Qt::ISODate)
              && st2.value(QStringLiteral("stopReason")).toString() == QStringLiteral("Nebenwirkung"), "Absetzen zurücklesen");
        QVariantMap st3 = st2; st3.insert(QStringLiteral("archived"), false);
        check(app.saveMedication(pid, st3) && app.medication(pid, mid).value(QStringLiteral("endDate")).toString().isEmpty(), "Wieder aufnehmen");
        m_testMed = mid;
    }
    {   // Peak Flow: Normwerttabelle (ERS 1994) und Warnhinweise
        check(PeakFlow::normValue(QLatin1Char('m'), 50, 175).value_or(0) == 525, "PEF: Mann 50 J., 175 cm");
        check(PeakFlow::normValue(QLatin1Char('w'), 30, 160).value_or(0) == 407, "PEF: Frau 30 J., 160 cm");
        check(PeakFlow::normValue(QLatin1Char('w'), 10, 135).value_or(0) == 282, "PEF: Kind 10 J., 135 cm");
        check(PeakFlow::normValue(QLatin1Char('m'), 52.5, 177.5).value_or(0) == 528, "PEF: interpoliert");
        check(PeakFlow::normValue(QLatin1Char('m'), 92, 170).value_or(0) == 429, "PEF: über 80 J. = Zeile 80");
        check(!PeakFlow::normValue(QLatin1Char('m'), 4, 150) && !PeakFlow::normValue(QLatin1Char('w'), 40, 130)
              && !PeakFlow::normValue(QChar(), 40, 170), "PEF: außerhalb der Tabelle");
        check(PeakFlow::zoneFor(80) == PeakFlow::Zone::Green && PeakFlow::zoneFor(79) == PeakFlow::Zone::Yellow
              && PeakFlow::zoneFor(50) == PeakFlow::Zone::Yellow && PeakFlow::zoneFor(49) == PeakFlow::Zone::Red, "PEF: Bereiche");
        Patient t; t.birthDate = QDate::currentDate().addYears(-40).addDays(-1); t.sex = QStringLiteral("d");
        check(!PeakFlow::reference(t, QDate::currentDate(), 170.0).missing.isEmpty(), "PEF: divers ohne Wahl → Hinweis");
        PeakFlow::setBiologicalSex(t, QLatin1Char('w'));
        check(PeakFlow::reference(t, QDate::currentDate(), 170.0).value == 422, "PEF: divers mit Tabelle weiblich");
        {   // biologisches Geschlecht: folgt „Geschlecht“, nur Abweichungen werden gespeichert
            Patient q; q.sex = QStringLiteral("m");
            PeakFlow::setBiologicalSex(q, QLatin1Char('m'));
            check(PeakFlow::bioSexSetting(q).isNull() && PeakFlow::biologicalSex(q) == QLatin1Char('m') && !PeakFlow::bioSexDiffers(q), "Bio-Geschlecht folgt");
            PeakFlow::setBiologicalSex(q, QLatin1Char('w'));
            check(PeakFlow::biologicalSex(q) == QLatin1Char('w') && PeakFlow::bioSexDiffers(q), "Bio-Geschlecht abweichend");
        }
        PeakFlow::setPersonalBest(t, 400);
        const PeakFlow::Assessment y = PeakFlow::assess(t, 260, QDate::currentDate(), 170.0);
        check(y.ref.source == PeakFlow::Source::PersonalBest && y.percent == 65 && y.zone == PeakFlow::Zone::Yellow && !y.advice.isEmpty(), "PEF: Bestwert, gelb");
        // über die Brücke: Bestwert im Stammblatt, Prüfung beim Erfassen, Markierung im Verlauf
        QVariantMap d = app.personDetail(pid);
        const int oldBest = d.value(QStringLiteral("peakFlowBest")).toInt();
        const QString oldSex = d.value(QStringLiteral("bioSex")).toString();
        d.insert(QStringLiteral("peakFlowBest"), 500);
        check(app.savePerson(d) == pid && app.personDetail(pid).value(QStringLiteral("peakFlowBest")).toInt() == 500, "PEF: Bestwert speichern");
        qInfo("PEF-Info: %s", qPrintable(app.peakFlowInfo(app.personDetail(pid))));
        check(app.peakFlowCheck(pid, 450, QString(), QDate::currentDate().toString(Qt::ISODate)).value(QStringLiteral("zone")).toString() == QStringLiteral("green"), "PEF: grün");
        check(app.peakFlowCheck(pid, 300, QString(), QString()).value(QStringLiteral("zone")).toString() == QStringLiteral("yellow"), "PEF: gelb");
        const QVariantMap red = app.peakFlowCheck(pid, 200, QString(), QString());
        check(red.value(QStringLiteral("zone")).toString() == QStringLiteral("red") && red.value(QStringLiteral("percent")).toInt() == 40
              && !red.value(QStringLiteral("advice")).toString().isEmpty(), "PEF: rot mit Hinweis");
        QVariantMap v = app.newVital(pid);
        v.insert(QStringLiteral("values"), QVariantMap{ { QStringLiteral("peakflow"), 210 } });
        const qint64 vid = app.saveVital(pid, v);
        bool marked = false;
        for (const QVariant &e : app.journal(pid, QString(), 2))
            if (e.toMap().value(QStringLiteral("id")).toLongLong() == vid)
                for (const QVariant &x : e.toMap().value(QStringLiteral("values")).toList())
                    marked |= !x.toMap().value(QStringLiteral("alert")).toString().isEmpty();
        check(vid > 0 && marked, "PEF: roter Wert im Verlauf markiert");
        app.deleteVital(vid);
        d = app.personDetail(pid);
        d.insert(QStringLiteral("peakFlowBest"), oldBest); d.insert(QStringLiteral("bioSex"), oldSex);
        app.savePerson(d);
    }
    qint64 rhythmMed = 0;
    {   // Einnahme-Rhythmus, PZN, Kennzeichnung (wie am Computer)
        const QString ibu = QStringLiteral("Ibuprofen");
        QVariantMap m = app.medication(pid, 0);
        check(m.value(QStringLiteral("rhythmChoice")).toInt() == 0 && m.value(QStringLiteral("rhythmWeekdays")).toList().size() == 7, "Rhythmus: Vorgabe täglich");
        m.insert(QStringLiteral("name"), QStringLiteral("Rhythmomed")); m.insert(QStringLiteral("substance"), ibu);
        m.insert(QStringLiteral("strength"), QStringLiteral("400 mg")); m.insert(QStringLiteral("morning"), QStringLiteral("1"));
        m.insert(QStringLiteral("rhythmChoice"), 2); m.insert(QStringLiteral("rhythmInterval"), 1);
        m.insert(QStringLiteral("rhythmWeekdays"), QVariantList{ false, false, false, false, false, false, false });
        check(!app.saveMedication(pid, m), "Rhythmus: ohne Wochentag abgelehnt");
        m.insert(QStringLiteral("rhythmWeekdays"), QVariantList{ true, false, false, true, false, false, false });
        m.insert(QStringLiteral("rhythmStart"), QDate::currentDate().toString(Qt::ISODate));
        const QVariantMap pv = app.medicationPreview(m);
        qInfo("Rhythmus-Vorschau: %s | %s | %s", qPrintable(pv.value(QStringLiteral("rhythmText")).toString()),
              qPrintable(pv.value(QStringLiteral("nextDays")).toString()), qPrintable(pv.value(QStringLiteral("amount")).toString()));
        check(!pv.value(QStringLiteral("nextDays")).toString().isEmpty(), "Rhythmus: Vorschau nächste Tage");
        const QVariantList cls = pv.value(QStringLiteral("classes")).toList();
        check(cls.size() == 5 && cls.value(1).toMap().value(QStringLiteral("auto")).toBool(), "Kennzeichnung: Ibuprofen = Schmerzmittel");
        m.insert(QStringLiteral("pzn"), QStringLiteral("PZN-1234562"));
        check(app.pznStatus(QStringLiteral("1234562")) == 1 && app.pznStatus(QStringLiteral("12345679")) == -1 && app.pznStatus(QString()) == 0, "PZN prüfen");
        m.insert(QStringLiteral("classes"), QVariantList{ false, false, false, false, true });   // Schmerzmittel abgewählt, Kortison gesetzt
        check(app.saveMedication(pid, m), "Rhythmus speichern");
        for (const QVariant &x : app.medications(pid, false)) if (x.toMap().value(QStringLiteral("name")) == QStringLiteral("Rhythmomed")) rhythmMed = x.toMap().value(QStringLiteral("id")).toLongLong();
        const QVariantMap r = app.medication(pid, rhythmMed);
        qInfo("Rhythmus gespeichert: %s, PZN %s", qPrintable(r.value(QStringLiteral("rhythm")).toString()), qPrintable(r.value(QStringLiteral("pzn")).toString()));
        const QVariantList wd = r.value(QStringLiteral("rhythmWeekdays")).toList();
        check(r.value(QStringLiteral("rhythmChoice")).toInt() == 2 && wd.value(0).toMap().value(QStringLiteral("on")).toBool()
              && wd.value(3).toMap().value(QStringLiteral("on")).toBool() && !wd.value(1).toMap().value(QStringLiteral("on")).toBool(), "Rhythmus zurücklesen");
        check(r.value(QStringLiteral("pzn")).toString() == QStringLiteral("01234562"), "PZN normalisiert");
        const QVariantList rc = r.value(QStringLiteral("classes")).toList();
        check(!rc.value(1).toMap().value(QStringLiteral("on")).toBool() && rc.value(4).toMap().value(QStringLiteral("on")).toBool(), "Kennzeichnung festgelegt");
        QVariantMap e = r;           // alle 3 Tage
        e.insert(QStringLiteral("rhythmChoice"), 1); e.insert(QStringLiteral("rhythmInterval"), 3);
        e.insert(QStringLiteral("classes"), QVariantList{ false, true, false, false, false });
        check(app.saveMedication(pid, e) && app.medication(pid, rhythmMed).value(QStringLiteral("rhythmChoice")).toInt() == 1
              && app.medication(pid, rhythmMed).value(QStringLiteral("rhythmInterval")).toInt() == 3, "Rhythmus alle 3 Tage");
        e = app.medication(pid, rhythmMed);
        e.insert(QStringLiteral("rhythmChoice"), 2); e.insert(QStringLiteral("rhythmInterval"), 1);
        e.insert(QStringLiteral("rhythmWeekdays"), QVariantList{ true, false, false, true, false, false, false });
        e.insert(QStringLiteral("classes"), QVariantList{ false, true, false, false, false });
        check(app.saveMedication(pid, e), "Rhythmus Mo/Do");
        QVariantMap b = app.medication(pid, 0);     // Bedarfsmedikation: Rhythmus wird ignoriert
        b.insert(QStringLiteral("name"), QStringLiteral("Bedarfomed")); b.insert(QStringLiteral("category"), 1);
        b.insert(QStringLiteral("rhythmChoice"), 1); b.insert(QStringLiteral("rhythmInterval"), 5);
        check(app.saveMedication(pid, b), "Bedarfsmedikation speichern");
        qint64 bid = 0;
        for (const QVariant &x : app.medications(pid, false)) if (x.toMap().value(QStringLiteral("name")) == QStringLiteral("Bedarfomed")) bid = x.toMap().value(QStringLiteral("id")).toLongLong();
        check(bid && app.medication(pid, bid).value(QStringLiteral("rhythm")).toString().isEmpty(), "Bedarf: kein Rhythmus");
        app.deleteMedication(pid, bid);
    }
    for (const char *k : { "sheet", "medplan", "pharmacy", "journal", "journalXlsx", "chart" }) {
        const QString f = app.exportReport(QString::fromLatin1(k), pid, QVariantMap{ { QStringLiteral("days"), 30 }, { QStringLiteral("scale"), 2 },
                                                                                   { QStringLiteral("panels"), QStringList{ QStringLiteral("bp") } } });
        check(!f.isEmpty() && QFileInfo(f).size() > 1000, k);
    }
    check(app.latest(pid).size() >= 3, "Übersicht");
    check(app.upcoming(pid, 30).size() >= 1, "Termine");
    {   // v0.12.0: Sprache zur Laufzeit umschalten, Kurzanleitung vorhanden
        const QString l0 = app.language();
        app.setLanguage(QStringLiteral("en"));
        check(app.language() == QStringLiteral("en") && app.languageChosen(), "Sprache Englisch");
        app.setLanguage(l0);
        check(app.language() == l0, "Sprache zurück");
        check(QFile::exists(QStringLiteral(VP_SOURCE_DOCS "/VitaParms-ErsteSchritte.pdf"))
              && QFile::exists(QStringLiteral(VP_SOURCE_DOCS "/VitaParms-GettingStarted.pdf")), "Erste Schritte (PDF) vorhanden");
    }
    {   // v0.14.0: Messzeitpunkt als Freitext
        QVariantMap v = app.newVital(pid);
        v.insert(QStringLiteral("values"), QVariantMap{ { QStringLiteral("pulse"), 77 } });
        v.insert(QStringLiteral("context"), QStringLiteral("nach dem Spaziergang"));
        const qint64 id = app.saveVital(pid, v);
        check(id > 0 && app.vital(id).value(QStringLiteral("context")).toString() == QStringLiteral("nach dem Spaziergang"), "Freitext-Messzeitpunkt");
        check(app.deleteVital(id), "Freitext-Messung löschen");
    }
    {   // v0.14.0: Terminplaner
        const QDate base = QDate::currentDate().addDays(3);
        QVariantMap a = app.appointment(0, pid);
        check(a.value(QStringLiteral("remindMinutes")).toInt() == 60 && a.value(QStringLiteral("patientId")).toLongLong() == pid, "Termin: Vorgaben");
        a.insert(QStringLiteral("title"), QStringLiteral("Tablet-Testtermin"));
        a.insert(QStringLiteral("startDate"), base.toString(Qt::ISODate));
        a.insert(QStringLiteral("startTime"), QStringLiteral("10:30"));
        a.insert(QStringLiteral("endDate"), base.toString(Qt::ISODate));
        a.insert(QStringLiteral("endTime"), QStringLiteral("11:00"));
        a.insert(QStringLiteral("location"), QStringLiteral("Praxis"));
        a.insert(QStringLiteral("priority"), 2);
        check(app.saveAppointment(a), "Termin speichern");
        QVariantList l = app.appointmentList(pid, QStringLiteral("upcoming"), false);
        qint64 tid = 0;
        for (const QVariant &v : l) if (v.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Tablet-Testtermin")) tid = v.toMap().value(QStringLiteral("id")).toLongLong();
        check(tid > 0, "Termin in der Liste");
        QVariantMap bad = app.appointment(tid, pid);
        bad.insert(QStringLiteral("endTime"), QStringLiteral("09:00"));
        check(!app.saveAppointment(bad), "Ende vor Beginn abgelehnt");
        check(app.setAppointmentDone(tid, true) && app.appointment(tid, pid).value(QStringLiteral("done")).toBool(), "Termin erledigt");
        check(app.appointmentList(pid, QStringLiteral("upcoming"), false).size() == l.size() - 1, "Erledigte ausgeblendet");
        check(app.setAppointmentDone(tid, false), "Termin wieder offen");
        // Erinnerung in zwei Stunden → geplant
        QVariantMap soon = app.appointment(0, pid);
        const QDateTime in3h = QDateTime::currentDateTime().addSecs(3 * 3600);
        soon.insert(QStringLiteral("title"), QStringLiteral("Tablet-Erinnerung"));
        soon.insert(QStringLiteral("startDate"), in3h.date().toString(Qt::ISODate));
        soon.insert(QStringLiteral("startTime"), in3h.time().toString(QStringLiteral("HH:mm")));
        soon.insert(QStringLiteral("endDate"), in3h.addSecs(1800).date().toString(Qt::ISODate));
        soon.insert(QStringLiteral("endTime"), in3h.addSecs(1800).time().toString(QStringLiteral("HH:mm")));
        const int before = app.plannedReminders();
        check(app.saveAppointment(soon) && app.plannedReminders() == before + 1, "Erinnerung geplant");
        // Serie: alle 2 Wochen, 4 Termine
        QVariantMap s = app.appointment(0, pid);
        s.insert(QStringLiteral("title"), QStringLiteral("Tablet-Serie"));
        s.insert(QStringLiteral("startDate"), base.toString(Qt::ISODate));
        s.insert(QStringLiteral("startTime"), QStringLiteral("08:00"));
        s.insert(QStringLiteral("endDate"), base.toString(Qt::ISODate));
        s.insert(QStringLiteral("endTime"), QStringLiteral("08:15"));
        s.insert(QStringLiteral("rule"), QVariantMap{ { QStringLiteral("repeat"), 6 }, { QStringLiteral("interval"), 2 },
                                                      { QStringLiteral("weekdays"), 1 << (base.dayOfWeek() - 1) },
                                                      { QStringLiteral("endMode"), 2 }, { QStringLiteral("count"), 4 } });
        check(app.apptRulePreview(s.value(QStringLiteral("rule")).toMap(), base.toString(Qt::ISODate)).contains(QStringLiteral("2")), "Serie: Vorschau");
        check(app.saveAppointment(s), "Serie speichern");
        auto seriesItems = [&]() {
            QVariantList r;
            for (const QVariant &v : app.appointmentList(pid, QStringLiteral("upcoming"), true))
                if (v.toMap().value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Tablet-Serie"))) r << v;
            return r;
        };
        QVariantList si = seriesItems();
        check(si.size() == 4 && si.first().toMap().value(QStringLiteral("series")).toBool(), "Serie: 4 Termine");
        // zweiten Termin einzeln ändern
        QVariantMap second = app.appointment(si.value(1).toMap().value(QStringLiteral("id")).toLongLong(), pid);
        check(second.value(QStringLiteral("isSeries")).toBool() && second.value(QStringLiteral("rule")).toMap().value(QStringLiteral("repeat")).toInt() == 6, "Serie: Regel gelesen");
        check(!app.apptRuleChanged(second), "Serie: Regel unverändert erkannt");
        second.insert(QStringLiteral("title"), QStringLiteral("Tablet-Serie (verschoben)"));
        second.insert(QStringLiteral("startTime"), QStringLiteral("09:00"));
        second.insert(QStringLiteral("endTime"), QStringLiteral("09:15"));
        check(app.saveAppointment(second, QStringLiteral("only")), "Serie: nur diesen ändern");
        si = seriesItems();
        int moved = 0;
        for (const QVariant &v : si) if (v.toMap().value(QStringLiteral("title")).toString().contains(QStringLiteral("verschoben"))) ++moved;
        check(si.size() == 4 && moved == 1, "Serie: einzeln geändert");
        // Regel ändern → nicht „nur diesen“
        QVariantMap third = app.appointment(si.value(2).toMap().value(QStringLiteral("id")).toLongLong(), pid);
        QVariantMap r3 = third.value(QStringLiteral("rule")).toMap();
        r3.insert(QStringLiteral("count"), 6);
        third.insert(QStringLiteral("rule"), r3);
        check(app.apptRuleChanged(third), "Serie: Regeländerung erkannt");
        check(!app.saveAppointment(third, QStringLiteral("only")), "Serie: Regeländerung nicht für einen Termin");
        // Rest ab dem dritten löschen
        check(app.deleteAppointment(si.value(2).toMap().value(QStringLiteral("id")).toLongLong(), QStringLiteral("following")), "Serie: ab hier löschen");
        check(seriesItems().size() == 2, "Serie: zwei übrig");
        m_testSeries = si.value(0).toMap().value(QStringLiteral("id")).toLongLong();
        m_testAppt = tid;
        for (const QVariant &v : app.appointmentList(pid, QStringLiteral("upcoming"), true))
            if (v.toMap().value(QStringLiteral("title")).toString() == QStringLiteral("Tablet-Erinnerung"))
                check(app.deleteAppointment(v.toMap().value(QStringLiteral("id")).toLongLong()), "Erinnerungstermin löschen");
    }
    {   // v0.16: Kontakte und Vorsorge auf dem Tablet bearbeiten
        QVariantMap d = app.personDetail(pid);
        QVariantList cl = d.value(QStringLiteral("contactList")).toList();
        const int before = int(cl.size());
        cl << QVariantMap{ { QStringLiteral("kind"), 2 }, { QStringLiteral("name"), QStringLiteral("Dr. Tablet") },
                           { QStringLiteral("specialty"), QStringLiteral("Augenheilkunde") }, { QStringLiteral("phone"), QStringLiteral("0621 123") } };
        d.insert(QStringLiteral("contactList"), cl);
        QVariantList adv = d.value(QStringLiteral("advanceDocs")).toList();
        check(adv.size() == 3, "Vorsorge: drei Dokumente");
        QVariantMap a0 = adv.value(1).toMap();
        const QVariantMap a0before = a0;
        a0.insert(QStringLiteral("present"), true);
        a0.insert(QStringLiteral("person"), QStringLiteral("Tablet Testperson"));
        adv[1] = a0;
        d.insert(QStringLiteral("advanceDocs"), adv);
        check(app.savePerson(d) == pid, "Kontakt und Vorsorge speichern");
        QVariantMap back = app.personDetail(pid);
        bool found = false;
        for (const QVariant &v : back.value(QStringLiteral("contactList")).toList())
            if (v.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("Dr. Tablet")) found = v.toMap().value(QStringLiteral("specialty")).toString() == QStringLiteral("Augenheilkunde");
        check(found && back.value(QStringLiteral("contactList")).toList().size() == before + 1, "Kontakt zurücklesen");
        check(back.value(QStringLiteral("advanceDocs")).toList().value(1).toMap().value(QStringLiteral("person")).toString() == QStringLiteral("Tablet Testperson"), "Vorsorge zurücklesen");
        // zurücksetzen
        QVariantList cl2;
        for (const QVariant &v : back.value(QStringLiteral("contactList")).toList())
            if (v.toMap().value(QStringLiteral("name")).toString() != QStringLiteral("Dr. Tablet")) cl2 << v;
        back.insert(QStringLiteral("contactList"), cl2);
        QVariantList adv2 = back.value(QStringLiteral("advanceDocs")).toList();
        adv2[1] = a0before;
        back.insert(QStringLiteral("advanceDocs"), adv2);
        check(app.savePerson(back) == pid && app.personDetail(pid).value(QStringLiteral("contactList")).toList().size() == before, "Kontakt entfernen");
    }
    {   // v0.16: BZ-Tagesprofil
        QVariantList pts = app.glucoseProfilePoints();
        check(pts.size() == 7, "BZ-Profil: sieben Zeitpunkte");
        const char *std[] = { "07:00", "09:30", "12:00", "14:30", "18:00", "20:30", "22:00" };
        for (int i = 0; i < pts.size() && i < 7; ++i) {      // Standard (ein früherer Testlauf hat sie verändert)
            QVariantMap m = pts[i].toMap();
            m.insert(QStringLiteral("active"), true);
            m.insert(QStringLiteral("time"), QString::fromLatin1(std[i]));
            pts[i] = m;
        }
        const QString tomorrow = QDate::currentDate().addDays(1).toString(Qt::ISODate);
        check(app.startGlucoseProfile(pid, tomorrow, pts, true) == 7 && app.glucoseProfileOpen(pid, tomorrow) == 7, "BZ-Profil: gestartet");
        check(app.stopGlucoseProfile(pid, tomorrow) == 7 && app.glucoseProfileOpen(pid, tomorrow) == 0, "BZ-Profil: beendet");
        app.quarterDue();
        app.quarterBanner();
    }
    qInfo("Brücke geprüft: %d Fehler", fails);

    shot(QStringLiteral("m00_start"));
    QObject *h = home();
    if (!h) { qWarning("HomePage nicht gefunden"); return 3; }
    const char *tabs[] = { "m01_uebersicht", "m02_messwerte", "m03_verlauf", "m04_stammblatt", "m05_medikamente", "m06_termine" };
    for (int i = 0; i < 6; ++i) {
        QQmlProperty::write(h, QStringLiteral("tab"), i);
        shot(QString::fromLatin1(tabs[i]));
    }
    // Termine: neuer Termin, Serie bearbeiten, Rückfrage „Für welche Termine?“
    call(h, "editAppointment", 0);
    shot(QStringLiteral("m17_termin_neu"));
    stackPop();
    call(h, "editAppointment", m_testSeries);
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("apptRepeat")))
        check(QQmlProperty::read(f, QStringLiteral("currentIndex")).toInt() == 5, "Serie: Auswahl „alle … Wochen“");
    shot(QStringLiteral("m18_termin_serie"));
    if (QObject *p = win->findChild<QObject *>(QStringLiteral("appointmentEditPage"))) QMetaObject::invokeMethod(p, "save");
    shot(QStringLiteral("m19_termin_serie_frage"));
    if (QObject *b = win->findChild<QObject *>(QStringLiteral("scopeAll"))) QMetaObject::invokeMethod(b, "clicked");
    wait(300);
    check(win->findChild<QObject *>(QStringLiteral("appointmentEditPage")) == nullptr, "Serie: gespeichert und geschlossen");
    // BZ-Tagesprofil: Seite und fällige Messung (Zeitpunkt in drei Minuten)
    {
        QQmlProperty::write(h, QStringLiteral("tab"), 1);
        if (QObject *b = win->findChild<QObject *>(QStringLiteral("glucoseProfileButton"))) QMetaObject::invokeMethod(b, "clicked");
        else check(false, "BZ-Profil-Knopf");
        wait(300);
        check(win->findChild<QObject *>(QStringLiteral("glucoseProfilePage")) != nullptr, "BZ-Profil-Seite");
        shot(QStringLiteral("m49_bz_tagesprofil"));
        stackPop();
        QVariantList pts = app.glucoseProfilePoints();
        const QTime soon = QTime::currentTime().addSecs(180);
        for (int i = 0; i < pts.size(); ++i) {
            QVariantMap m = pts[i].toMap();
            m.insert(QStringLiteral("active"), i == 2);
            if (i == 2) m.insert(QStringLiteral("time"), soon.toString(QStringLiteral("HH:mm")));
            pts[i] = m;
        }
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        if (soon > QTime::currentTime()) {        // nicht kurz vor Mitternacht
            check(app.startGlucoseProfile(pid, today, pts, false) == 1, "BZ-Profil: ein Zeitpunkt heute");
            const QVariantMap due = app.dueProfileMeasurement();
            check(due.value(QStringLiteral("id")).toLongLong() > 0 && due.value(QStringLiteral("patientId")).toLongLong() == pid, "BZ-Profil: fällige Messung");
            QMetaObject::invokeMethod(win, "checkDue");
            wait(300);
            QObject *q = win->findChild<QObject *>(QStringLiteral("profileQuestion"));
            check(q && QQmlProperty::read(q, QStringLiteral("visible")).toBool(), "BZ-Profil: Rückfrage");
            shot(QStringLiteral("m48_bz_frage"));
            if (q) { QMetaObject::invokeMethod(q, "close"); QMetaObject::invokeMethod(win, "measureProfile", Q_ARG(QVariant, due)); }
            wait(300);
            QObject *mp = nullptr;
            for (QObject *o : win->findChildren<QObject *>()) if (o->property("profileApptId").toLongLong() == due.value(QStringLiteral("id")).toLongLong()) mp = o;
            check(mp && mp->property("context").toString() == due.value(QStringLiteral("context")).toString(), "BZ-Profil: Messzeitpunkt vorbelegt");
            shot(QStringLiteral("m48b_bz_messung"));
            stackPop();
            check(app.stopGlucoseProfile(pid, today) == 1, "BZ-Profil: aufräumen");
        }
        // Stammblatt: Kontakt hinzufügen
        call(h, "editPerson", false);
        wait(300);
        if (QObject *b = win->findChild<QObject *>(QStringLiteral("addContactButton"))) {
            if (auto *it = qobject_cast<QQuickItem *>(b)) {
                for (QQuickItem *p = it->parentItem(); p; p = p->parentItem())
                    if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, it->mapToItem(p->childItems().value(0), QPointF()).y() - 300)); break; }
            }
            shot(QStringLiteral("m11b_stammblatt_kontakte"));
            QMetaObject::invokeMethod(b, "clicked");
            wait(300);
            shot(QStringLiteral("m11c_kontakt_neu"));
        } else check(false, "Kontakt-Knopf");
        stackPop();
    }
    QQmlProperty::write(h, QStringLiteral("tab"), 0);
    wait(3000);                                   // Meldung „Termin gespeichert“ ausblenden lassen
    call(h, "measure", 0);
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("contextFreeText")))
        QQmlProperty::write(f, QStringLiteral("text"), app.language() == QStringLiteral("en") ? QStringLiteral("after the walk")
                                                                                         : QStringLiteral("nach dem Spaziergang"));
    else check(false, "Freitext-Feld");
    shot(QStringLiteral("m10_messung_neu"));
    stackPop();
    {   // Peak Flow im roten Bereich: Hinweis beim Erfassen
        call(h, "measure", 0);
        QQuickItem *card = nullptr, *pg = nullptr;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *it) {
            if (!it || card) return;
            if (it->property("key").toString() == QStringLiteral("peakflow") && it->property("text").isValid()) { card = it; return; }
            for (QQuickItem *c : it->childItems()) walk(c);
        };
        walk(win->contentItem());
        for (QQuickItem *it = card; it; it = it->parentItem())
            if (it->property("pfCheck").isValid()) { pg = it; break; }
        if (card && pg) {
            QQmlProperty::write(card, QStringLiteral("text"), QStringLiteral("150"));
            QMetaObject::invokeMethod(pg, "updatePeakFlow");
            wait(200);
            const QVariantMap c = pg->property("pfCheck").toMap();
            check(c.value(QStringLiteral("zone")).toString() == QStringLiteral("red") || !c.value(QStringLiteral("text")).toString().isEmpty(), "PEF-Hinweis im Formular");
            for (QQuickItem *p = card->parentItem(); p; p = p->parentItem())
                if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, card->mapToItem(p->childItems().value(0), QPointF()).y() - 200)); break; }
            shot(QStringLiteral("m10b_peakflow_rot"));
        } else check(false, "PEF-Feld");
        stackPop();
    }
    call(h, "editPerson", false);
    shot(QStringLiteral("m11_stammblatt_bearbeiten"));
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("peakFlowInfo"))) {
        if (auto *fl = qobject_cast<QQuickItem *>(f))
            for (QQuickItem *p = fl->parentItem(); p; p = p->parentItem())
                if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, fl->mapToItem(p->childItems().value(0), QPointF()).y() - 300)); break; }
        shot(QStringLiteral("m11d_stammblatt_peakflow"));
    } else check(false, "PEF im Stammblatt");
    // zum Abschnitt Pflegegrad/Ausweis scrollen
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("personFlick"))) {
        QQmlProperty::write(f, QStringLiteral("contentY"), 700);
        shot(QStringLiteral("m12_stammblatt_pflege"));
    }
    stackPop();
    // Messwert bearbeiten (jüngste Messung)
    const QVariantList j = app.journal(QQmlProperty::read(h, QStringLiteral("personId")).toLongLong());
    if (!j.isEmpty()) {
        call(h, "measure", j.first().toMap().value(QStringLiteral("id")));
        shot(QStringLiteral("m13_messung_bearbeiten"));
        stackPop();
    }
    QMetaObject::invokeMethod(h, "openSettings");
    shot(QStringLiteral("m14_einstellungen"));
    stackPop();
    // USB-Abgleich: Seite öffnen, über die Schleife (127.0.0.1) mit der eigenen Datenbank abgleichen
    QMetaObject::invokeMethod(win, "openUsbSync");
    wait(300);
    check(app.usbActive() && app.usbCode().size() == 7, "USB-Abgleich wartet mit Code");
    shot(QStringLiteral("m44_usb_abgleich"));
    {
        UsbSyncClient client;
        UsbSyncClient::Info info;
        QString err;
        check(!client.connectTo(QStringLiteral("127.0.0.1"), UsbSync::kDevicePort, QStringLiteral("000000"), &info, &err), "USB: falscher Code abgelehnt");
        check(client.connectTo(QStringLiteral("127.0.0.1"), UsbSync::kDevicePort, app.usbCode(), &info, &err), "USB: verbinden");
        check(info.loggedIn && info.sameDatabase, "USB: gleiche Datenbank erkannt");
        UsbSyncClient::Result r;
        check(client.merge(&r, &err) && r.localInserted + r.remoteInserted == 0, "USB: Abgleich");
        if (!err.isEmpty()) qWarning("USB: %s", qPrintable(err));
        wait(200);
        check(app.usbConnected(), "USB: verbunden angezeigt");
        shot(QStringLiteral("m45_usb_verbunden"));
        client.disconnectFromDevice();
        wait(200);
    }
    stackPop();
    check(app.usbActive(), "USB: Code bleibt bis zum Programmende gültig");
    {   // Seite zu, angemeldet: Computer mit derselben Datenbank verbindet sich ohne Code
        UsbSyncClient client;
        UsbSyncClient::Info info;
        QString err;
        check(client.connectTo(QStringLiteral("127.0.0.1"), UsbSync::kDevicePort, QString(), &info, &err) && info.sameDatabase,
              "USB: ohne Code verbunden");
        if (!err.isEmpty()) qWarning("USB ohne Code: %s", qPrintable(err));
        UsbSyncClient::Result r;
        check(client.merge(&r, &err), "USB: ohne Code abgeglichen");
        client.disconnectFromDevice();
        wait(200);
        UsbSyncClient again;
        check(again.connectTo(QStringLiteral("127.0.0.1"), UsbSync::kDevicePort, app.usbCode(), &info, &err),
              "USB: Code nach Schließen der Seite noch gültig");
        again.disconnectFromDevice();
        wait(200);
    }
    // Medikament mit Erinnerung
    QQmlProperty::write(h, QStringLiteral("personId"), pid);
    call(h, "editMed", m_testMed);
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("remindCheck"))) {
        check(QQmlProperty::read(f, QStringLiteral("checked")).toBool(), "Haken im Formular");
        if (auto *fl = qobject_cast<QQuickItem *>(f)) {      // zum Abschnitt scrollen
            for (QQuickItem *p = fl->parentItem(); p; p = p->parentItem())
                if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, fl->mapToItem(p->childItems().value(0), QPointF()).y() - 80)); break; }
        }
    } else check(false, "Haken im Formular");
    shot(QStringLiteral("m15_medikament_erinnerung"));
    stackPop();
    // Medikament mit Einnahme-Rhythmus (Mo, Do) und Kennzeichnung
    call(h, "editMed", rhythmMed);
    if (QObject *f = win->findChild<QObject *>(QStringLiteral("rhythmCombo"))) {
        check(QQmlProperty::read(f, QStringLiteral("currentIndex")).toInt() == 2, "Rhythmus im Formular");
        if (auto *fl = qobject_cast<QQuickItem *>(f)) {
            for (QQuickItem *p = fl->parentItem(); p; p = p->parentItem())
                if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, fl->mapToItem(p->childItems().value(0), QPointF()).y() - 260)); break; }
        }
    } else check(false, "Rhythmus im Formular");
    shot(QStringLiteral("m15b_medikament_rhythmus"));
    stackPop();
    app.deleteMedication(pid, rhythmMed);
    QQmlProperty::write(h, QStringLiteral("tab"), 4);
    shot(QStringLiteral("m16_medikamente_erinnerung"));
    check(app.deleteMedication(pid, m_testMed), "Medikament löschen");

    // ---- Krankenakte (Stufe 3): Fotos → PDF, Datei übernehmen, Liste, Suche, Ansicht, Teilen
    {
        const bool en = app.language() == QStringLiteral("en");
        const QString tmpDir = out + QStringLiteral("/akte");
        QDir().mkpath(tmpDir);
        auto page = [&](const QString &name, const QStringList &lines, const QColor &accent) {
            QImage img(1240, 1754, QImage::Format_RGB32);     // A4 bei 150 dpi, wie ein Foto/Scan
            img.fill(QColor(0xfb, 0xfa, 0xf6));
            QPainter p(&img);
            p.fillRect(0, 0, img.width(), 150, accent);
            QFont f(QStringLiteral("Sans")); f.setPixelSize(54); f.setBold(true); p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRect(70, 0, 1100, 150), Qt::AlignVCenter, lines.value(0));
            f.setPixelSize(34); f.setBold(false); p.setFont(f); p.setPen(QColor(0x22, 0x22, 0x22));
            int y = 260;
            for (int i = 1; i < lines.size(); ++i, y += 64) p.drawText(70, y, lines.at(i));
            p.setPen(QColor(0xbb, 0xbb, 0xbb));
            for (; y < 1600; y += 56) p.drawLine(70, y, 1170, y);
            p.end();
            const QString path = tmpDir + QLatin1Char('/') + name;
            img.save(path, "JPG", 85);
            return path;
        };
        const QString p1 = page(QStringLiteral("brief1.jpg"), en ? QStringList{ QStringLiteral("Cardiology practice"), QStringLiteral("Discharge letter"), QStringLiteral("Findings: sinus rhythm, BP 135/85") }
                                                               : QStringList{ QStringLiteral("Kardiologische Praxis"), QStringLiteral("Arztbrief"), QStringLiteral("Befund: Sinusrhythmus, RR 135/85") }, QColor(0x1f, 0x78, 0xb9));
        const QString p2 = page(QStringLiteral("brief2.jpg"), en ? QStringList{ QStringLiteral("Page 2"), QStringLiteral("Recommendation: check-up in 6 months") }
                                                               : QStringList{ QStringLiteral("Seite 2"), QStringLiteral("Empfehlung: Kontrolle in 6 Monaten") }, QColor(0x1f, 0x78, 0xb9));
        app.draftClear();
        check(app.draftAddImage(p1) && app.draftAddFiles({ QUrl::fromLocalFile(p2) }).value(QStringLiteral("ok")).toBool(), "Akte: zwei Fotos");
        QVariantMap di = app.draftInfo(pid);
        check(di.value(QStringLiteral("kind")).toString() == QStringLiteral("images") && di.value(QStringLiteral("pages")).toInt() == 2, "Akte: Entwurf zwei Seiten");
        app.draftRotatePage(1); app.draftRotatePage(1); app.draftRotatePage(1); app.draftRotatePage(1);
        check(app.saveDraft(pid, { { QStringLiteral("title"), QString() } }) == 0, "Akte: Titel nötig");
        const QString t1 = en ? QStringLiteral("Cardiology letter") : QStringLiteral("Arztbrief Kardiologie");
        const qint64 d1 = app.saveDraft(pid, { { QStringLiteral("title"), t1 }, { QStringLiteral("category"), en ? QStringLiteral("Doctor's letter") : QStringLiteral("Arztbrief") },
                                               { QStringLiteral("issuer"), en ? QStringLiteral("Cardiology practice") : QStringLiteral("Kardiologische Praxis") },
                                               { QStringLiteral("date"), QDate::currentDate().addDays(-12).toString(Qt::ISODate) } });
        check(d1 > 0, "Akte: Fotos als PDF gespeichert");
        const QVariantMap info = app.documentInfo(d1);
        qInfo("Akte: %s, %d Seiten, %s", qPrintable(info.value(QStringLiteral("title")).toString()), info.value(QStringLiteral("pages")).toInt(),
              qPrintable(info.value(QStringLiteral("size")).toString()));
        check(info.value(QStringLiteral("pages")).toInt() == 2 && info.value(QStringLiteral("hasData")).toBool(), "Akte: zwei Seiten im PDF");
        check(Database::instance().documentData(d1).startsWith("%PDF"), "Akte: Inhalt ist PDF");
        // PDF-Datei übernehmen; dieselbe Datei ein zweites Mal wird erkannt
        const QString pdfPath = tmpDir + QStringLiteral("/labor.pdf");
        {
            QFile f(pdfPath);
            check(f.open(QIODevice::WriteOnly) && f.write(PdfTools::imagesToPdf({ QImage(page(QStringLiteral("labor.jpg"),
                      en ? QStringList{ QStringLiteral("Laboratory"), QStringLiteral("Blood count"), QStringLiteral("HbA1c 6.4 %") }
                         : QStringList{ QStringLiteral("Labor"), QStringLiteral("Blutbild"), QStringLiteral("HbA1c 6,4 %") }, QColor(0x2e, 0x7d, 0x32))) },
                      QStringLiteral("Labor"))) > 0, "Akte: Test-PDF");
        }
        check(app.draftAddFiles({ QUrl::fromLocalFile(pdfPath) }).value(QStringLiteral("ok")).toBool()
              && app.draftInfo(pid).value(QStringLiteral("kind")).toString() == QStringLiteral("pdf"), "Akte: PDF-Datei als Entwurf");
        const qint64 d2 = app.saveDraft(pid, { { QStringLiteral("title"), en ? QStringLiteral("Blood count") : QStringLiteral("Blutbild") },
                                               { QStringLiteral("category"), en ? QStringLiteral("Lab results") : QStringLiteral("Laborwerte") },
                                               { QStringLiteral("date"), QDate::currentDate().addDays(-3).toString(Qt::ISODate) } });
        check(d2 > 0, "Akte: PDF übernommen");
        app.draftAddFiles({ QUrl::fromLocalFile(pdfPath) });
        check(app.saveDraft(pid, { { QStringLiteral("title"), QStringLiteral("Doppelt") } }) == 0 && app.lastError().contains(en ? QStringLiteral("already") : QStringLiteral("bereits")), "Akte: Dublette erkannt");
        app.draftClear();
        // Liste, Suche, Art, Angaben ändern
        check(app.documents(pid, QString(), QString()).size() >= 2, "Akte: Liste");
        check(app.documents(pid, en ? QStringLiteral("cardio") : QStringLiteral("kardio"), QString()).size() == 1, "Akte: Suche");
        {
            const QString labCat = en ? QStringLiteral("Lab results") : QStringLiteral("Laborwerte");
            const QVariantList labs = app.documents(pid, QString(), labCat);
            bool allLab = !labs.isEmpty();
            for (const QVariant &x : labs) allLab &= x.toMap().value(QStringLiteral("category")).toString() == labCat;
            check(allLab && labs.size() < app.documents(pid, QString(), QString()).size(), "Akte: Art");
        }
        for (const QVariant &x : app.documents(pid, QString(), QString())) {
            const qint64 id = x.toMap().value(QStringLiteral("id")).toLongLong();
            const QByteArray data = Database::instance().documentData(id);
            if (app.pdfPreviewAvailable() && x.toMap().value(QStringLiteral("hasData")).toBool() && app.documentPageImage(QString::number(id), 0, 140).isNull())
                qInfo("Akte: keine Vorschau für %lld (%s, %lld Bytes, Anfang %s)", id, qPrintable(x.toMap().value(QStringLiteral("title")).toString()),
                      qint64(data.size()), data.left(8).toHex().constData());
        }
        QVariantMap ed = app.documentInfo(d2);
        ed.insert(QStringLiteral("notes"), en ? QStringLiteral("HbA1c slightly raised – recheck in 3 months") : QStringLiteral("HbA1c leicht erhöht – Kontrolle in 3 Monaten"));
        check(app.saveDocument(ed) && app.documentInfo(d2).value(QStringLiteral("notes")).toString().contains(QStringLiteral("HbA1c")), "Akte: Angaben ändern");
        if (app.pdfPreviewAvailable())
            check(!app.documentPageImage(QString::number(d1), 1, 300).isNull(), "Akte: Seitenbild");
        // Dokument, dessen Inhalt noch auf einem anderen Gerät liegt (Abgleich steht aus)
        qint64 d3 = 0;
        {
            QByteArray pdf = "%PDF-1.4\n% fehlt\n";
            MedicalDocument md; md.patientId = pid; md.title = en ? QStringLiteral("X-ray report (from the computer)") : QStringLiteral("Röntgenbefund (vom Computer)");
            md.category = en ? QStringLiteral("Imaging") : QStringLiteral("Bildgebung"); md.docDate = QDate::currentDate().addDays(-30);
            md.sha256 = QCryptographicHash::hash(pdf, QCryptographicHash::Sha256); md.pages = 1;
            check(Database::instance().addDocument(md, pdf), "Akte: Dokument ohne Inhalt anlegen");
            SqlQuery q(Database::instance().handleDb());
            q.exec(QStringLiteral("DELETE FROM document_files WHERE document_id = %1").arg(md.id));
            d3 = md.id;
            check(!app.documentInfo(d3).value(QStringLiteral("hasData")).toBool() && !app.openDocument(d3), "Akte: Inhalt fehlt");
        }
        // Teilen: PDF-Dateien und verschlüsseltes ZIP mit Stammblatt
        const QVariantMap s1 = app.shareDocuments(pid, { d1, d2 }, {});
        check(s1.value(QStringLiteral("ok")).toBool() && s1.value(QStringLiteral("files")).toStringList().size() == 2, "Akte: als PDF teilen");
        const QVariantMap s2 = app.shareDocuments(pid, { d1, d2 }, { { QStringLiteral("zip"), true }, { QStringLiteral("password"), QStringLiteral("Geheim-Passwort-1") } });
        const QString zipPath = s2.value(QStringLiteral("files")).toStringList().value(0);
        QFile zf(zipPath);
        check(s2.value(QStringLiteral("ok")).toBool() && zf.open(QIODevice::ReadOnly) && zf.read(2) == "PK", "Akte: als ZIP teilen");
        qInfo("Akte: ZIP %s (%lld Bytes)", qPrintable(QFileInfo(zipPath).fileName()), QFileInfo(zipPath).size());
        check(!app.shareDocuments(pid, { d1 }, { { QStringLiteral("zip"), true }, { QStringLiteral("password"), QStringLiteral("kurz") } }).value(QStringLiteral("ok")).toBool(), "Akte: Passwort zu kurz");

        // Bildschirmfotos
        QQmlProperty::write(h, QStringLiteral("tab"), 6);
        wait(400);
        shot(QStringLiteral("m60_akte"));
        call(h, "openDocument", d1);
        wait(500);
        shot(QStringLiteral("m61_dokument"));
        stackPop();
        // neues Dokument: zwei Fotos als Entwurf
        call(h, "addDocument", QString());
        wait(300);
        if (QObject *ep = win->findChild<QObject *>(QStringLiteral("documentEditPage"))) {
            app.draftAddImage(p1);
            app.draftAddImage(p2);
            QMetaObject::invokeMethod(ep, "refreshDraft", Q_ARG(QVariant, true));
            if (QObject *t = win->findChild<QObject *>(QStringLiteral("docTitle")))
                QQmlProperty::write(t, QStringLiteral("text"), en ? QStringLiteral("Discharge letter, hospital") : QStringLiteral("Entlassbrief Klinikum"));
            wait(300);
            shot(QStringLiteral("m62_dokument_neu"));
        } else check(false, "Akte: Seite neues Dokument");
        stackPop();
        app.draftClear();
        call(h, "shareDocuments", QVariantList{ d1, d2 });
        wait(300);
        if (QObject *z = win->findChild<QObject *>(QStringLiteral("shareZip"))) QQmlProperty::write(z, QStringLiteral("checked"), true);
        wait(200);
        shot(QStringLiteral("m63_teilen"));
        if (QObject *sp = win->findChild<QObject *>(QStringLiteral("sharePopup"))) QMetaObject::invokeMethod(sp, "close");
        wait(200);
        check(app.deleteDocument(d1) && app.deleteDocument(d2) && app.deleteDocument(d3), "Akte: löschen");
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
    }

    // Hochformat (Tablet gedreht)
    win->resize(800, 1280);
    wait(300);
    QQmlProperty::write(h, QStringLiteral("tab"), 0);
    shot(QStringLiteral("m20_hoch_uebersicht"));
    QQmlProperty::write(h, QStringLiteral("tab"), 2);
    shot(QStringLiteral("m21_hoch_verlauf"));
    call(h, "measure", 0);
    shot(QStringLiteral("m22_hoch_messung"));
    stackPop();

    // Dunkel
    win->resize(1280, 800);
    app.setTheme(QStringLiteral("dark"));
    QQmlProperty::write(h, QStringLiteral("tab"), 0);
    shot(QStringLiteral("m30_dunkel_uebersicht"));
    QQmlProperty::write(h, QStringLiteral("tab"), 2);
    shot(QStringLiteral("m31_dunkel_verlauf"));
    call(h, "measure", 0);
    shot(QStringLiteral("m32_dunkel_messung"));
    stackPop();
    app.setTheme(QStringLiteral("light"));

    // Testtermine entfernen
    check(app.deleteAppointment(m_testSeries, QStringLiteral("all")) && app.deleteAppointment(m_testAppt), "Testtermine löschen");
    // Sperren mit Rückfrage zum USB-Abgleich
    app.setUsbAskOnExit(true);
    QQmlProperty::write(win, QStringLiteral("usbSynced"), false);     // der USB-Test oben hat schon abgeglichen
    if (QObject *b = win->findChild<QObject *>(QStringLiteral("lockButton"))) {
        QMetaObject::invokeMethod(b, "clicked");
        wait(300);
        shot(QStringLiteral("m46_usb_frage_sperren"));
        QObject *q = win->findChild<QObject *>(QStringLiteral("usbQuestion"));
        check(q && QQmlProperty::read(q, QStringLiteral("visible")).toBool(), "Rückfrage USB-Abgleich beim Sperren");
        if (q) QMetaObject::invokeMethod(q, "close");
        wait(200);
        // „Beenden“ fragt ebenfalls (auch nach einem Abgleich in dieser Sitzung)
        QQmlProperty::write(win, QStringLiteral("usbSynced"), true);
        if (QObject *qb = win->findChild<QObject *>(QStringLiteral("quitButton"))) {
            QMetaObject::invokeMethod(qb, "clicked");
            wait(300);
            check(q && QQmlProperty::read(q, QStringLiteral("visible")).toBool(), "Rückfrage USB-Abgleich beim Beenden");
            shot(QStringLiteral("m47_usb_frage_beenden"));
            if (q) QMetaObject::invokeMethod(q, "close");
        } else check(false, "Beenden-Knopf");
    } else check(false, "Sperren-Knopf");
    // Gesperrt → Anmeldung
    app.lock();
    QMetaObject::invokeMethod(win, "goStart");
    shot(QStringLiteral("m40_anmeldung"));

    // Datenbank lokal übertragen: als *.vpdb exportieren und wieder importieren
    {
        app.login(qEnvironmentVariable("VP_AUTOPILOT_USER"), qEnvironmentVariable("VP_AUTOPILOT_PASSWORD"));
        const int before = int(app.persons().size());
        const QString bundle = out + QStringLiteral("/test.vpdb");
        QString err;
        check(DbTransfer::exportBundle(bundle, &err) && QFileInfo(bundle).size() > 4096, "Export *.vpdb");
        app.lock();
        check(app.isDatabaseBundle(QUrl::fromLocalFile(bundle).toString()), "Bundle erkannt");
        check(!app.importDatabase(QUrl::fromLocalFile(out + QStringLiteral("/m00_start.png")).toString(), QString()), "Falsche Datei abgelehnt");
        check(app.importDatabase(QUrl::fromLocalFile(bundle).toString(), QString()), "Import *.vpdb");
        check(app.login(qEnvironmentVariable("VP_AUTOPILOT_USER"), qEnvironmentVariable("VP_AUTOPILOT_PASSWORD")), "Anmeldung nach Import");
        check(int(app.persons().size()) == before, "Daten nach Import");
        app.lock();
        QMetaObject::invokeMethod(win, "goStart");
        wait(300);
        if (QObject *lp = win->findChild<QObject *>(QStringLiteral("loginPage"))) QMetaObject::invokeMethod(lp, "importRequested");
        shot(QStringLiteral("m41_import_datei"));
        check(win->findChild<QObject *>(QStringLiteral("gettingStartedButton")) != nullptr, "Erste Schritte auf der Einrichtungsseite");
        // v0.12.0: Einrichtung (Auswahl) mit „Erste Schritte“ und Sprachwahl beim ersten Start
        for (QObject *o : win->findChildren<QObject *>())
            if (o->property("mode").isValid() && o->metaObject()->indexOfProperty("dbFile") >= 0) o->setProperty("mode", 0);
        wait(300);
        shot(QStringLiteral("m42_einrichten"));
        QMetaObject::invokeMethod(win, "showLanguagePage");
        wait(400);
        check(win->findChild<QObject *>(QStringLiteral("languagePage")) != nullptr, "Sprachwahl-Seite");
        shot(QStringLiteral("m43_sprache"));
        qInfo("Import geprüft: %d Fehler", fails);
    }
    return fails;
}
