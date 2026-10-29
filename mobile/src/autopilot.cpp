// VitaParms Tablet – automatischer Rundgang für Bildschirmfotos und Tests
// Nur im Testbau (qmake CONFIG+=autopilot); nicht Teil der ausgelieferten App.
//   VP_AUTOPILOT_DB   Datenbank (mit .keys), VP_AUTOPILOT_USER / _PASSWORD, VP_AUTOPILOT_OUT Zielordner
// SPDX-License-Identifier: MIT
#include "appcontroller.h"
#include "dbtransfer.h"
#include "usbsync.h"
#include "peakflow.h"
#include "stool.h"
#include "fluid.h"
#include <QLocale>
#include "database.h"
#include "keystore.h"
#include "session.h"
#include "websync.h"
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
        {   // Stuhlgang: Häkchen + Bristol-Auswahl, Angaben fürs Stuhlprotokoll, Protokolle als PDF/Excel
        const QVariantMap ch = app.stoolChoices();
        check(ch.value(QStringLiteral("types")).toList().size() == 8 && ch.value(QStringLiteral("colors")).toList().size() >= 6, "Stuhl: Auswahl");
        QVariantMap v = app.newVital(pid);
        v.insert(QStringLiteral("values"), QVariantMap{});
        v.insert(QStringLiteral("stool"), 5);
        v.insert(QStringLiteral("stoolDetail"), QVariantMap{ { QStringLiteral("amount"), QStringLiteral("viel") },
                                                             { QStringLiteral("color"), QStringLiteral("hellbraun") },
                                                             { QStringLiteral("symptoms"), QStringLiteral("Blähungen") } });
        check(app.checkVital(v).isEmpty(), "Stuhl: allein ein gültiger Eintrag");
        const qint64 vid = app.saveVital(pid, v);
        const QVariantMap back = app.vital(vid);
        check(vid > 0 && back.value(QStringLiteral("stool")).toInt() == 5
              && back.value(QStringLiteral("stoolDetail")).toMap().value(QStringLiteral("color")).toString() == QStringLiteral("hellbraun"), "Stuhl: speichern/laden");
        bool listed = false;
        for (const QVariant &e : app.journal(pid, QString(), 2))
            if (e.toMap().value(QStringLiteral("id")).toLongLong() == vid)
                for (const QVariant &x : e.toMap().value(QStringLiteral("values")).toList())
                    listed |= x.toMap().value(QStringLiteral("text")).toString().contains(QStringLiteral("5"));
        check(listed, "Stuhl: im Verlauf");
        check(app.stoolNeedsAttention(QVariantMap{ { QStringLiteral("color"), Stool::colorChoices().value(6) } })
              && !app.stoolNeedsAttention(QVariantMap{ { QStringLiteral("color"), Stool::colorChoices().value(0) } }), "Stuhl: schwarz → Hinweis");
        check(app.stoolStatus(pid).value(QStringLiteral("days")).toInt() == 0 && !app.stoolStatus(pid).value(QStringLiteral("warn")).toBool(), "Stuhl: Status heute");
        for (const char *k : { "stool", "stoolXlsx", "glucose", "glucoseXlsx" }) {
            const QString f = app.exportReport(QString::fromLatin1(k), pid, QVariantMap{ { QStringLiteral("days"), 30 } });
            check(!f.isEmpty() && QFileInfo(f).size() > 1000, qPrintable(QStringLiteral("Protokoll %1").arg(QLatin1String(k))));
            if (qstrcmp(k, "stool") == 0 && qEnvironmentVariableIsSet("VP_AUTOPILOT_OUT"))
                QFile::copy(f, qEnvironmentVariable("VP_AUTOPILOT_OUT") + QStringLiteral("/stuhlprotokoll_tablet.pdf"));
        }
        {   // Formular
            call(h, "measure", 0);
            wait(300);
            QObject *chk = win->findChild<QObject *>(QStringLiteral("stoolCheck"));
            QQuickItem *sec = win->findChild<QQuickItem *>(QStringLiteral("stoolSection"));
            if (chk && sec) {
                QQmlProperty::write(chk, QStringLiteral("checked"), true);
                for (QQuickItem *it = sec; it; it = it->parentItem())
                    if (it->property("stoolCombo").isValid()) { if (auto *cb = it->property("stoolCombo").value<QObject *>()) cb->setProperty("currentIndex", 4); break; }
                if (QObject *a = win->findChild<QObject *>(QStringLiteral("stoolAmount"))) QQmlProperty::write(a, QStringLiteral("text"), Stool::amountChoices().value(1));
                if (QObject *c = win->findChild<QObject *>(QStringLiteral("stoolColor"))) QQmlProperty::write(c, QStringLiteral("text"), Stool::colorChoices().value(0));
                wait(200);
                for (QQuickItem *p = sec->parentItem(); p; p = p->parentItem())
                    if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, sec->mapToItem(p->childItems().value(0), QPointF()).y() - 120)); break; }
                wait(200);
                shot(QStringLiteral("m10c_stuhlgang"));
            } else check(false, "Stuhl: Formularfelder");
            stackPop();
        }
        app.deleteVital(vid);
    }
    {   // INR, Ein- und Ausfuhr, Ödeme
        const QVariantMap fc = app.fluidChoices();
        check(fc.value(QStringLiteral("edemaGrades")).toList().size() == 5 && fc.value(QStringLiteral("inKinds")).toList().size() >= 4, "Bilanz: Auswahl");
        QVariantMap d = app.personDetail(pid);
        const QVariantMap old = d;
        d.insert(QStringLiteral("inrLow"), QLocale().toString(2.5, 'f', 1)); d.insert(QStringLiteral("inrHigh"), QLocale().toString(3.5, 'f', 1));
        d.insert(QStringLiteral("fluidMin"), QStringLiteral("1200")); d.insert(QStringLiteral("fluidMax"), QString());
        d.insert(QStringLiteral("weightWatch"), QLocale().toString(2.0, 'f', 1));
        check(app.savePerson(d) == pid, "Bilanz: Stammblatt speichern");
        const QVariantMap back = app.personDetail(pid);
        check(back.value(QStringLiteral("inrHigh")).toString() == QLocale().toString(3.5, 'f', 1)
              && back.value(QStringLiteral("fluidMin")).toString() == QStringLiteral("1200") && back.value(QStringLiteral("fluidMax")).toString().isEmpty(), "Bilanz: Zielwerte zurück");
        check(app.inrCheck(pid, QStringLiteral("3,9")).value(QStringLiteral("zone")).toString() == QStringLiteral("high")
              || app.inrCheck(pid, QStringLiteral("3.9")).value(QStringLiteral("zone")).toString() == QStringLiteral("high"), "INR: über dem Zielbereich");
        QVariantMap v = app.newVital(pid);
        v.insert(QStringLiteral("values"), QVariantMap{ { QStringLiteral("inr"), 3.9 }, { QStringLiteral("fluidIn"), 250 }, { QStringLiteral("edema"), 2 } });
        v.insert(QStringLiteral("fluidInKind"), Fluid::inKinds().value(0));
        v.insert(QStringLiteral("edemaSite"), Fluid::edemaSites().value(1));
        v.insert(QStringLiteral("edemaSide"), Fluid::edemaSides().value(1));
        const qint64 vid = app.saveVital(pid, v);
        const QVariantMap rb = app.vital(vid);
        check(vid > 0 && rb.value(QStringLiteral("values")).toMap().value(QStringLiteral("edema")).toInt() == 2
              && rb.value(QStringLiteral("edemaSide")).toString() == Fluid::edemaSides().value(1)
              && rb.value(QStringLiteral("fluidInKind")).toString() == Fluid::inKinds().value(0), "Bilanz: speichern/laden");
        const QStringList cw = app.careWarnings(pid, vid);
        qInfo("Hinweise nach dem Speichern: %s", qPrintable(cw.join(QStringLiteral(" | "))));
        check(cw.size() >= 2, "Bilanz: Hinweise INR + Ödeme");
        check(!app.careNotices(pid).isEmpty(), "Bilanz: Hinweis in der Übersicht");
        check(!app.fluidDay(pid, rb).value(QStringLiteral("text")).toString().isEmpty(), "Bilanz: heute");
        bool inrRow = false;
        for (const QVariant &e : app.journal(pid, QString(), 2))
            if (e.toMap().value(QStringLiteral("id")).toLongLong() == vid)
                for (const QVariant &x : e.toMap().value(QStringLiteral("values")).toList())
                    inrRow |= x.toMap().value(QStringLiteral("label")).toString() == QStringLiteral("INR") && !x.toMap().value(QStringLiteral("alert")).toString().isEmpty();
        check(inrRow, "INR im Verlauf markiert");
        for (const char *k : { "fluid", "fluidXlsx" }) {
            const QString f = app.exportReport(QString::fromLatin1(k), pid, QVariantMap{ { QStringLiteral("days"), 14 } });
            check(!f.isEmpty() && QFileInfo(f).size() > 1000, qPrintable(QStringLiteral("Protokoll %1").arg(QLatin1String(k))));
        }
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
        wait(500);
        shot(QStringLiteral("m01b_uebersicht_hinweise"));
        {   // Formular: INR, Ein-/Ausfuhr, Ödeme
            call(h, "measure", 0);
            wait(300);
            QQuickItem *sec = win->findChild<QQuickItem *>(QStringLiteral("fluidSection"));
            QQuickItem *pg = nullptr;
            for (QQuickItem *it = sec; it; it = it->parentItem()) if (it->property("edemaGrade").isValid()) { pg = it; break; }
            if (sec && pg) {
                std::function<QQuickItem *(QQuickItem *)> card = [&](QQuickItem *it) -> QQuickItem * {
                    if (!it) return nullptr;
                    if (it->property("key").toString() == QStringLiteral("inr") && it->property("text").isValid()) return it;
                    for (QQuickItem *c : it->childItems()) if (QQuickItem *x = card(c)) return x;
                    return nullptr;
                };
                if (QQuickItem *c = card(win->contentItem())) QQmlProperty::write(c, QStringLiteral("text"), QLocale().toString(3.9, 'f', 1));
                if (QObject *f = win->findChild<QObject *>(QStringLiteral("fluidIn"))) QQmlProperty::write(f, QStringLiteral("text"), QStringLiteral("250"));
                if (QObject *f = win->findChild<QObject *>(QStringLiteral("fluidOut"))) QQmlProperty::write(f, QStringLiteral("text"), QStringLiteral("300"));
                pg->setProperty("edemaGrade", 2);
                if (QObject *f = win->findChild<QObject *>(QStringLiteral("edemaSite"))) QQmlProperty::write(f, QStringLiteral("text"), Fluid::edemaSites().value(1));
                pg->setProperty("edemaSide", Fluid::edemaSides().value(1));
                QMetaObject::invokeMethod(pg, "updateCare");
                wait(250);
                check(!pg->property("edemaHint").toString().isEmpty() && pg->property("inrState").toMap().value(QStringLiteral("zone")).toString() == QStringLiteral("high"),
                      "Bilanz: Hinweise im Formular");
                for (QQuickItem *p = sec->parentItem(); p; p = p->parentItem())
                    if (p->inherits("QQuickFlickable")) { QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, sec->mapToItem(p->childItems().value(0), QPointF()).y() - 140)); break; }
                wait(200);
                shot(QStringLiteral("m10d_bilanz_oedeme"));
            } else check(false, "Bilanz: Formularfelder");
            stackPop();
        }
        app.deleteVital(vid);
        app.savePerson(old);
    }
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
        QQmlProperty::write(h, QStringLiteral("tab"), 7);
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

    // ---- Medikamente stellen, Bestand und Bestellungen, Vorsorge-PDF (Stufe 4)
    {
        const bool en = app.language() == QStringLiteral("en");
        auto isoIn = [](int days) { return QDate::currentDate().addDays(days).toString(Qt::ISODate); };
        QList<qint64> regular;
        for (const QVariant &v : app.medications(pid, false)) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("category")).toInt() == 0 && !m.value(QStringLiteral("morning")).toString().isEmpty())
                regular << m.value(QStringLiteral("id")).toLongLong();
        }
        check(regular.size() >= 2, "Stellen: Regelmedikation");
        const qint64 m1 = regular.value(0), m2 = regular.value(1);
        auto medStock = [&](qint64 id) {
            for (const QVariant &v : app.stockOverview(pid).value(QStringLiteral("meds")).toList())
                if (v.toMap().value(QStringLiteral("id")).toLongLong() == id) return v.toMap();
            return QVariantMap();
        };
        auto pkgQty = [&](qint64 id) {
            double q = 0;
            for (const QVariant &v : app.stockPackages(pid, id).value(QStringLiteral("packages")).toList()) q += v.toMap().value(QStringLiteral("qty")).toDouble();
            return q;
        };
        check(!medStock(m1).value(QStringLiteral("tracked")).toBool(), "Bestand: zunächst nicht erfasst");
        check(app.receivePackage(pid, m1, 100, isoIn(700), en ? QStringLiteral("Pharmacy, N3") : QStringLiteral("Apotheke, N3")), "Bestand: Packung einbuchen");
        check(app.receivePackage(pid, m1, 20, isoIn(-5), QString()), "Bestand: verfallene Packung");
        check(app.receivePackage(pid, m2, 6, isoIn(400), QString()), "Bestand: kleine Packung");
        check(!app.receivePackage(pid, m2, 0, QString(), QString()), "Bestand: Menge nötig");
        const QVariantMap s1 = medStock(m1);
        check(s1.value(QStringLiteral("tracked")).toBool() && !s1.value(QStringLiteral("expired")).toString().isEmpty(), "Bestand: Übersicht mit Verfall");
        const int st2 = medStock(m2).value(QStringLiteral("state")).toInt();
        check(st2 == 3 || st2 == 4, "Bestand: kleine Packung → jetzt bestellen");
        // Stellen: Tagesdosierer, alle Zeitpunkte
        const QString today = isoIn(0);
        const QVariantMap ses = app.prepSession(pid, 1, -1, today);
        const QVariantList items = ses.value(QStringLiteral("items")).toList();
        check(!items.isEmpty() && !ses.value(QStringLiteral("intro")).toString().isEmpty(), "Stellen: Auswahl");
        check(!items.value(0).toMap().value(QStringLiteral("speech")).toString().isEmpty(), "Stellen: Vorlesetext");
        QVariantList all;
        for (int i = 0; i < items.size(); ++i) all << i;
        const double before = pkgQty(m1);
        const QVariantMap fin = app.prepFinish(pid, 1, -1, today, all, true, QStringLiteral("Autopilot"));
        check(fin.value(QStringLiteral("ok")).toBool(), "Stellen: Protokoll");
        check(pkgQty(m1) < before, "Stellen: Abbuchung vom Bestand");
        check(!fin.value(QStringLiteral("warnings")).toStringList().isEmpty(), "Stellen: Hinweis nachbestellen");
        const QVariantList log = app.preparationLog(pid, 1);
        check(!log.isEmpty() && log.value(0).toMap().value(QStringLiteral("complete")).toBool()
              && log.value(0).toMap().value(QStringLiteral("note")).toString() == QStringLiteral("Autopilot"), "Stellen: Protokoll lesen");
        // abgebrochener Durchgang: nur bestätigte Medikamente
        const QVariantMap part = app.prepFinish(pid, 1, 0, today, { 0 }, false, QString());
        check(part.value(QStringLiteral("ok")).toBool() && !app.preparationLog(pid, 1).value(0).toMap().value(QStringLiteral("complete")).toBool(), "Stellen: abgebrochen");
        // Wochendosierer
        app.setPrepPlan(pid, 7, 1);
        check(app.prepInfo(pid).value(QStringLiteral("weekly")).toBool() && QDate::fromString(app.prepWeekStart(pid), Qt::ISODate).dayOfWeek() == 1, "Stellen: Wochenrhythmus");
        const QVariantMap wk = app.prepSession(pid, 7, -1, app.prepWeekStart(pid));
        check(wk.value(QStringLiteral("items")).toList().value(0).toMap().value(QStringLiteral("week")).toList().size() == 7, "Stellen: Woche mit 7 Tagen");
        // Bestellung: offen → bestellt → geliefert
        const QStringList rem = app.stockReminder(pid);
        check(!rem.isEmpty(), "Bestellung: Erinnerung");
        QVariantMap open;
        for (const QVariant &v : app.stockOverview(pid).value(QStringLiteral("orders")).toList())
            if (v.toMap().value(QStringLiteral("status")).toInt() == 0) open = v.toMap();
        check(!open.isEmpty(), "Bestellung: offene Liste");
        const qint64 orderId = open.value(QStringLiteral("id")).toLongLong();
        const QVariantMap item = open.value(QStringLiteral("items")).toList().value(0).toMap();
        check(app.setOrderItemAmount(pid, item.value(QStringLiteral("id")).toLongLong(), item.value(QStringLiteral("medId")).toLongLong(),
                                     en ? QStringLiteral("1 pack N3 (100)") : QStringLiteral("1 Packung N3 (100 Stück)")), "Bestellung: Menge");
        const QString orderPdf = app.exportReport(QStringLiteral("order"), pid, { { QStringLiteral("orderId"), orderId } });
        QFile of(orderPdf);
        check(of.open(QIODevice::ReadOnly) && of.read(5) == "%PDF-", "Bestellung: PDF");
        const QString logPdf = app.exportReport(QStringLiteral("preplog"), pid, { { QStringLiteral("days"), 30 } });
        QFile lf(logPdf);
        check(lf.open(QIODevice::ReadOnly) && lf.read(5) == "%PDF-", "Stellen: Protokoll als PDF");

        // Bildschirmfotos (vor dem Bestätigen: offene Bestellliste)
        QQmlProperty::write(h, QStringLiteral("tab"), 4);
        wait(400);
        shot(QStringLiteral("m70_medikamente_bestand"));
        app.setPrepPlan(pid, 1, 1);
        QMetaObject::invokeMethod(h, "openPrep");
        wait(600);
        QObject *pp = win->findChild<QObject *>(QStringLiteral("prepPage"));
        check(pp && QQmlProperty::read(pp, QStringLiteral("items")).toList().size() == items.size(), "Stellen: Seite");
        if (pp) {
            QMetaObject::invokeMethod(pp, "next");
            wait(300);
            shot(QStringLiteral("m71_stellen"));
            if (QObject *w = win->findChild<QObject *>(QStringLiteral("prepWeekly"))) QMetaObject::invokeMethod(w, "clicked");
            wait(500);
            check(QQmlProperty::read(pp, QStringLiteral("days")).toInt() == 7, "Stellen: Wochendosierer gewählt");
            QMetaObject::invokeMethod(pp, "showCard");
            wait(300);
            shot(QStringLiteral("m72_stellen_woche"));
            if (QObject *dd = win->findChild<QObject *>(QStringLiteral("prepDaily"))) QMetaObject::invokeMethod(dd, "clicked");
            wait(300);
            const int n = QQmlProperty::read(pp, QStringLiteral("items")).toList().size();
            for (int i = 0; i < n; ++i) { QMetaObject::invokeMethod(pp, "next"); wait(60); }
            wait(400);
            check(QQmlProperty::read(pp, QStringLiteral("finished")).toBool(), "Stellen: gerichtet");
            shot(QStringLiteral("m73_gerichtet"));
            QMetaObject::invokeMethod(pp, "close");
            wait(3600);                       // Hinweis „Medikamente sind gerichtet“ ausblenden lassen
        }
        QMetaObject::invokeMethod(h, "openStock", Q_ARG(QVariant, 0));
        wait(600);
        if (QObject *sp = win->findChild<QObject *>(QStringLiteral("stockPage"))) {
            shot(QStringLiteral("m74_bestand"));
            QQmlProperty::write(sp, QStringLiteral("tab"), 1);
            wait(400);
            shot(QStringLiteral("m75_bestellungen"));
            QMetaObject::invokeMethod(sp, "closed", Q_ARG(QString, QString()));
            wait(400);
        } else check(false, "Bestand: Seite");

        check(app.confirmOrder(orderId, today, en ? QStringLiteral("ordered at the pharmacy") : QStringLiteral("bei der Apotheke bestellt")), "Bestellung: bestätigen");
        check(app.stockReminder(pid).isEmpty(), "Bestellung: keine Erinnerung mehr");
        const double q2 = pkgQty(m2);
        for (const QVariant &v : app.stockOverview(pid).value(QStringLiteral("orders")).toList()) {
            const QVariantMap o = v.toMap();
            if (o.value(QStringLiteral("id")).toLongLong() != orderId) continue;
            check(o.value(QStringLiteral("status")).toInt() == 1, "Bestellung: Status bestellt");
            for (const QVariant &iv : o.value(QStringLiteral("items")).toList())
                if (iv.toMap().value(QStringLiteral("medId")).toLongLong() == m2)
                    check(app.deliverOrderItem(orderId, iv.toMap().value(QStringLiteral("id")).toLongLong(), 50, isoIn(500)), "Bestellung: Lieferung");
        }
        check(pkgQty(m2) >= q2 + 50 - 0.01, "Bestellung: Lieferung eingebucht");
        // Korrektur und Entsorgen
        const QVariantList pk = app.stockPackages(pid, m1).value(QStringLiteral("packages")).toList();
        const QVariantMap first = pk.value(0).toMap();
        check(app.correctPackage(pid, m1, first.value(QStringLiteral("id")).toLongLong(), 42, first.value(QStringLiteral("expiry")).toString(), QStringLiteral("gezählt")), "Bestand: korrigieren");
        for (const QVariant &v : app.stockPackages(pid, m1).value(QStringLiteral("packages")).toList())
            if (v.toMap().value(QStringLiteral("expired")).toBool())
                check(app.discardPackage(m1, v.toMap().value(QStringLiteral("id")).toLongLong()), "Bestand: entsorgen");
        check(medStock(m1).value(QStringLiteral("expired")).toString().isEmpty(), "Bestand: verfallene Packung entsorgt");

        // Vorsorge-PDF aus der Krankenakte verknüpfen
        const QString vTitle = en ? QStringLiteral("Lasting power of attorney") : QStringLiteral("Vorsorgevollmacht");
        QImage vimg(1240, 1754, QImage::Format_RGB32);
        vimg.fill(Qt::white);
        {
            QPainter vp(&vimg);
            QFont f(QStringLiteral("Sans")); f.setPixelSize(60); f.setBold(true); vp.setFont(f);
            vp.drawText(QRect(80, 120, 1080, 120), Qt::AlignLeft, vTitle);
        }
        app.draftClear();
        const QString vpath = out + QStringLiteral("/vorsorge.jpg");
        vimg.save(vpath, "JPG", 85);
        app.draftAddImage(vpath);
        const qint64 vdoc = app.saveDraft(pid, { { QStringLiteral("title"), vTitle },
                                                 { QStringLiteral("category"), QCoreApplication::translate("PatientDialog", "Vorsorge") },
                                                 { QStringLiteral("date"), isoIn(-400) } });
        check(vdoc > 0, "Vorsorge: Dokument in der Akte");
        check(app.advanceCandidates(pid).value(0).toMap().value(QStringLiteral("id")).toLongLong() == vdoc, "Vorsorge: Kategorie zuerst");
        check(app.linkAdvanceDocument(pid, 1, vdoc), "Vorsorge: verknüpfen");
        const QVariantMap pd = app.personDetail(pid);
        check(pd.value(QStringLiteral("advanceDocs")).toList().value(1).toMap().value(QStringLiteral("docId")).toLongLong() == vdoc
              && pd.value(QStringLiteral("advancePdfs")).toList().size() >= 1, "Vorsorge: zurücklesen");
        QQmlProperty::write(h, QStringLiteral("tab"), 3);
        wait(500);
        shot(QStringLiteral("m76_vorsorge_pdf"));
        QMetaObject::invokeMethod(h, "editPerson", Q_ARG(QVariant, false));
        wait(600);
        // Knöpfe aus einem Repeater hängen nur im Item-Baum (nicht als QObject-Kinder)
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
            if (!root) return nullptr;
            if (root->objectName() == name && root->isVisible()) return root;
            for (QQuickItem *c : root->childItems())
                if (QQuickItem *f = findItem(c, name)) return f;
            return nullptr;
        };
        if (QObject *lb = findItem(win->contentItem(), QStringLiteral("advanceLinkButton"))) {   // zum Abschnitt Vorsorge blättern
            if (auto *it = qobject_cast<QQuickItem *>(lb)) {
                for (QQuickItem *p = it->parentItem(); p; p = p->parentItem())
                    if (p->inherits("QQuickFlickable")) {
                        QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, it->mapToItem(p->childItems().value(0), QPointF()).y() - 330));
                        break;
                    }
            }
            wait(300);
            shot(QStringLiteral("m78_vorsorge_verknuepft"));
        } else check(false, "Vorsorge: Knopf im Stammblatt");
        if (QObject *pk2 = win->findChild<QObject *>(QStringLiteral("advancePicker"))) {
            QMetaObject::invokeMethod(pk2, "openFor", Q_ARG(QVariant, 1), Q_ARG(QVariant, vTitle));
            wait(400);
            shot(QStringLiteral("m77_vorsorge_waehlen"));
            QMetaObject::invokeMethod(pk2, "close");
        } else check(false, "Vorsorge: Auswahl");
        stackPop();
        check(app.linkAdvanceDocument(pid, 1, 0) && app.personDetail(pid).value(QStringLiteral("advanceDocs")).toList().value(1).toMap().value(QStringLiteral("docId")).toLongLong() == 0, "Vorsorge: lösen");
        check(app.deleteDocument(vdoc), "Vorsorge: Dokument löschen");
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
    }

    // ---- Wunddokumentation: Wunde bei Aufnahme, Chronik, Foto, Hinweise, Bericht (Stufe 7)
    {
        std::function<QQuickItem *(QQuickItem *, const QString &)> findItem = [&](QQuickItem *root, const QString &name) -> QQuickItem * {
            if (!root) return nullptr;
            if (root->objectName() == name && root->isVisible()) return root;
            for (QQuickItem *c : root->childItems())
                if (QQuickItem *f = findItem(c, name)) return f;
            return nullptr;
        };
        auto scrollTo = [&](QQuickItem *it, double above) {
            for (QQuickItem *p = it ? it->parentItem() : nullptr; p; p = p->parentItem())
                if (p->inherits("QQuickFlickable")) {
                    QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, it->mapToItem(p->childItems().value(0), QPointF()).y() - above));
                    break;
                }
        };
        auto isoIn = [](int days) { return QDate::currentDate().addDays(days).toString(Qt::ISODate); };
        const bool en = app.language() == QStringLiteral("en");
        const QVariantMap ch = app.woundChoices();
        check(ch.value(QStringLiteral("kinds")).toStringList().size() == 10 && ch.value(QStringLiteral("grades")).toList().value(0).toStringList().size() == 6,
              "Wunden: Auswahllisten");
        const QStringList sites = ch.value(QStringLiteral("sites")).toStringList();
        QVariantMap wm = app.woundData(pid, 0, true);
        check(wm.value(QStringLiteral("onAdmission")).toBool() && !wm.value(QStringLiteral("admissionDate")).toString().isEmpty(), "Wunden: Vorgaben bei Aufnahme");
        check(app.saveWound(pid, wm) == 0, "Wunden: Lokalisation Pflicht");
        wm.insert(QStringLiteral("site"), sites.value(4));          // Ferse
        wm.insert(QStringLiteral("side"), ch.value(QStringLiteral("sides")).toStringList().value(1));
        wm.insert(QStringLiteral("kind"), 0);
        wm.insert(QStringLiteral("grade"), 2);
        wm.insert(QStringLiteral("since"), isoIn(-30));
        wm.insert(QStringLiteral("origin"), ch.value(QStringLiteral("origins")).toStringList().value(1));
        wm.insert(QStringLiteral("photoConsent"), true);
        wm.insert(QStringLiteral("admissionDate"), isoIn(-25));
        wm.insert(QStringLiteral("limitations"), QStringList{ ch.value(QStringLiteral("limitations")).toStringList().value(0) });
        const qint64 wid = app.saveWound(pid, wm);
        check(wid > 0, "Wunden: bei Aufnahme angelegt");
        // Wundfoto (gezeichnet) mit Maßband
        auto photo = [&](int r) {
            QImage img(800, 600, QImage::Format_RGB32);
            img.fill(QColor(0xe6, 0xc0, 0xa2));
            QPainter pa(&img);
            pa.setRenderHint(QPainter::Antialiasing);
            pa.setPen(Qt::NoPen);
            pa.setBrush(QColor(0xc4, 0x52, 0x52)); pa.drawEllipse(QPointF(400, 280), r * 1.4, r);
            pa.setBrush(QColor(0xdc, 0xcd, 0x84)); pa.drawEllipse(QPointF(380, 270), r * 0.5, r * 0.35);
            pa.setBrush(Qt::white); pa.drawRect(100, 500, 600, 44);
            pa.setPen(Qt::black);
            for (int i = 0; i <= 15; ++i) pa.drawLine(100 + i * 40, 500, 100 + i * 40, i % 2 ? 512 : 522);
            pa.end();
            const QString path = out + QStringLiteral("/wunde_%1.jpg").arg(r);
            img.save(path, "JPG", 90);
            return path;
        };
        struct A { int day; const char *l, *w; int gran, fib; int trend; };
        const A as[] = { { -25, "3,5", "2,8", 30, 70, -1 }, { -11, "3,0", "2,2", 55, 45, 0 }, { -2, "2,4", "1,8", 75, 25, 0 } };
        for (const A &a : as) {
            QVariantMap e = app.woundEntryData(wid, 0, 0);
            e.insert(QStringLiteral("date"), isoIn(a.day));
            e.insert(QStringLiteral("time"), QStringLiteral("09:15"));
            e.insert(QStringLiteral("length"), QString::fromLatin1(a.l));
            e.insert(QStringLiteral("width"), QString::fromLatin1(a.w));
            e.insert(QStringLiteral("depth"), QStringLiteral("0,2"));
            e.insert(QStringLiteral("granulation"), a.gran);
            e.insert(QStringLiteral("fibrin"), a.fib);
            e.insert(QStringLiteral("exudate"), 1);
            e.insert(QStringLiteral("exudateKind"), ch.value(QStringLiteral("exudateKinds")).toStringList().value(0));
            e.insert(QStringLiteral("edge"), QStringList{ ch.value(QStringLiteral("edges")).toStringList().value(0) });
            e.insert(QStringLiteral("pain"), QStringLiteral("3"));
            e.insert(QStringLiteral("trend"), a.trend);
            e.insert(QStringLiteral("phase"), a.gran > 50 ? 1 : 0);
            e.insert(QStringLiteral("cleansing"), en ? QStringLiteral("Wound irrigation solution") : QStringLiteral("Wundspüllösung"));
            e.insert(QStringLiteral("dressing"), en ? QStringLiteral("Foam dressing") : QStringLiteral("Schaumverband"));
            e.insert(QStringLiteral("compression"), en ? QStringLiteral("Heel offloading") : QStringLiteral("Ferse frei lagern"));
            e.insert(QStringLiteral("intervalDays"), 3);
            e.insert(QStringLiteral("nextChange"), isoIn(a.day + 3));
            check(app.woundPhotoFrom(photo(int(QString::fromLatin1(a.l).replace(QLatin1Char(','), QLatin1Char('.')).toDouble() * 28))) > 0, "Wunden: Foto übernommen");
            check(app.saveWoundEntry(wid, e), "Wunden: Wundbeurteilung");
        }
        {
            QVariantMap e = app.woundEntryData(wid, 0, 1);
            e.insert(QStringLiteral("date"), isoIn(0));
            e.insert(QStringLiteral("time"), QStringLiteral("08:30"));
            check(e.value(QStringLiteral("dressing")).toString() == (en ? QStringLiteral("Foam dressing") : QStringLiteral("Schaumverband")), "Wunden: Versorgung übernommen");
            app.woundPhotoClear();
            QVariantMap empty = e; empty.insert(QStringLiteral("dressing"), QString());
            check(!app.saveWoundEntry(wid, empty), "Wunden: leerer Verbandwechsel abgelehnt");
            e.insert(QStringLiteral("description"), en ? QStringLiteral("Wound clean, edges pink") : QStringLiteral("Wunde sauber, Rand rosig"));
            e.insert(QStringLiteral("trend"), 0);
            check(app.saveWoundEntry(wid, e), "Wunden: Verbandwechsel");
        }
        const QVariantList ents = app.woundEntries(wid);
        check(ents.size() == 4 && ents.value(1).toMap().value(QStringLiteral("hasPhoto")).toBool(), "Wunden: Chronik");
        check(!app.woundImage(QStringLiteral("chart-%1").arg(wid), 600).isNull(), "Wunden: Flächenverlauf");
        // zweite Wunde mit Infektionszeichen → Hinweis in der Übersicht
        QVariantMap w2 = app.woundData(pid, 0, false);
        w2.insert(QStringLiteral("site"), sites.value(7));
        w2.insert(QStringLiteral("kind"), 1);
        const qint64 wid2 = app.saveWound(pid, w2);
        QVariantMap i2 = app.woundEntryData(wid2, 0, 0);
        i2.insert(QStringLiteral("time"), QStringLiteral("10:00"));
        i2.insert(QStringLiteral("length"), QStringLiteral("2"));
        i2.insert(QStringLiteral("width"), QStringLiteral("1,5"));
        i2.insert(QStringLiteral("infection"), QStringList{ ch.value(QStringLiteral("infection")).toStringList().value(0), ch.value(QStringLiteral("infection")).toStringList().value(1) });
        check(wid2 > 0 && app.saveWoundEntry(wid2, i2), "Wunden: zweite Wunde");
        bool woundNotice = false;
        for (const QVariant &v : app.careNotices(pid)) woundNotice |= v.toMap().value(QStringLiteral("wound")).toBool() && v.toMap().value(QStringLiteral("critical")).toBool();
        check(woundNotice, "Wunden: Hinweis in der Übersicht");
        // Bericht je Wunde
        bool listed = false;
        for (const QVariant &v : app.reportCatalog(pid)) listed |= v.toMap().value(QStringLiteral("kind")).toString() == QStringLiteral("wound-%1").arg(wid);
        check(listed, "Wunden: unter Berichte");
        const QString wpdf = app.createReport(pid, QStringLiteral("wound-%1").arg(wid), false, 0);
        QFile wf(wpdf);
        check(wf.open(QIODevice::ReadOnly) && wf.read(5) == "%PDF-", "Wunden: PDF");
        const QString wx = app.createReport(pid, QStringLiteral("wound-%1").arg(wid), true, 0);
        check(wx.endsWith(QLatin1String(".xlsx")) && QFileInfo(wx).size() > 2000, "Wunden: Excel");
        const QString sheet = app.exportReport(QStringLiteral("sheet"), pid);
        check(QFileInfo(sheet).size() > 1000, "Wunden: Stammblatt");

        // Bildschirmfotos
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
        wait(500);
        shot(QStringLiteral("m94_uebersicht_wunden"));
        QQmlProperty::write(h, QStringLiteral("tab"), 6);
        wait(500);
        if (QObject *wp = win->findChild<QObject *>(QStringLiteral("woundsPage"))) {
            QMetaObject::invokeMethod(wp, "select", Q_ARG(QVariant, wid));
            wait(500);
            shot(QStringLiteral("m95_wunden"));
            QQmlProperty::write(wp, QStringLiteral("contentY"), 420);
            wait(300);
            shot(QStringLiteral("m95b_wunden_chronik"));
            QQmlProperty::write(wp, QStringLiteral("contentY"), 0);
        } else check(false, "Wunden: Seite");
        QMetaObject::invokeMethod(h, "editWoundEntry", Q_ARG(QVariant, wid), Q_ARG(QVariant, 0), Q_ARG(QVariant, 0));
        wait(600);
        if (QObject *ep = win->findChild<QObject *>(QStringLiteral("woundEntryPage"))) {
            app.woundPhotoFrom(photo(56));
            QQmlProperty::write(ep, QStringLiteral("photoRev"), 99);
            QQmlProperty::write(ep, QStringLiteral("hasPhoto"), true);
            if (QQuickItem *f = findItem(win->contentItem(), QStringLiteral("entryLength"))) QQmlProperty::write(f, QStringLiteral("text"), QStringLiteral("2,0"));
            if (QQuickItem *f = findItem(win->contentItem(), QStringLiteral("entryWidth"))) QQmlProperty::write(f, QStringLiteral("text"), QStringLiteral("1,5"));
            wait(400);
            shot(QStringLiteral("m96_wundbeurteilung"));
            if (QQuickItem *t = findItem(win->contentItem(), QStringLiteral("entryInfection"))) scrollTo(t, 200);
            wait(300);
            shot(QStringLiteral("m96b_wundbeurteilung_zustand"));
            if (QQuickItem *t = findItem(win->contentItem(), QStringLiteral("entryPhoto"))) scrollTo(t, 520);
            wait(300);
            shot(QStringLiteral("m96c_wundbeurteilung_foto"));
            QMetaObject::invokeMethod(ep, "closed", Q_ARG(QString, QString()));
            wait(400);
        } else check(false, "Wunden: Seite Wundbeurteilung");
        QMetaObject::invokeMethod(h, "editWoundEntry", Q_ARG(QVariant, wid), Q_ARG(QVariant, 0), Q_ARG(QVariant, 1));
        wait(600);
        if (QObject *ep = win->findChild<QObject *>(QStringLiteral("woundEntryPage"))) {
            check(!QQmlProperty::read(ep, QStringLiteral("full")).toBool(), "Wunden: Verbandwechsel kurz");
            if (QQuickItem *f = findItem(win->contentItem(), QStringLiteral("entryDescription")))
                QQmlProperty::write(f, QStringLiteral("text"), en ? QStringLiteral("Edges epithelialising") : QStringLiteral("Ränder epithelisieren"));
            if (QQuickItem *t = findItem(win->contentItem(), QStringLiteral("trend_0"))) QMetaObject::invokeMethod(t, "clicked");
            wait(400);
            shot(QStringLiteral("m97_verbandwechsel"));
            QMetaObject::invokeMethod(ep, "closed", Q_ARG(QString, QString()));
            wait(400);
        } else check(false, "Wunden: Seite Verbandwechsel");
        QMetaObject::invokeMethod(h, "editWound", Q_ARG(QVariant, wid), Q_ARG(QVariant, false));
        wait(600);
        if (win->findChild<QObject *>(QStringLiteral("woundEditPage"))) {
            shot(QStringLiteral("m98_wunde_bearbeiten"));
            stackPop();
        } else check(false, "Wunden: Seite Wunde");
        QQmlProperty::write(h, QStringLiteral("tab"), 3);
        QMetaObject::invokeMethod(h, "editPerson", Q_ARG(QVariant, false));
        wait(700);
        if (QQuickItem *b = findItem(win->contentItem(), QStringLiteral("admissionWoundButton"))) {
            scrollTo(b, 330);
            wait(300);
            shot(QStringLiteral("m99_stammblatt_wunden"));
        } else check(false, "Wunden: Stammblatt-Abschnitt");
        stackPop();
        check(app.setWoundHealed(wid2) && app.woundList(pid, false).size() == 1, "Wunden: abgeheilt");
        check(app.deleteWound(wid2) && app.woundList(pid, true).size() == 1, "Wunden: löschen");
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
    }

    // ---- Ausgaben: Berichte öffnen/teilen, verschlüsseltes ZIP, Website aktualisieren (Stufe 5)
    {
        const QVariantList cat = app.reportCatalog(pid);
        QStringList kinds;
        for (const QVariant &v : cat) kinds << v.toMap().value(QStringLiteral("kind")).toString();
        check(kinds.contains(QStringLiteral("sheet")) && kinds.contains(QStringLiteral("pharmacy")) && kinds.contains(QStringLiteral("journal"))
              && kinds.contains(QStringLiteral("chart")) && kinds.contains(QStringLiteral("preplog")), "Berichte: Liste");
        auto head = [](const QString &f, int n) { QFile x(f); return x.open(QIODevice::ReadOnly) ? x.read(n) : QByteArray(); };
        int made = 0;
        for (const QVariant &v : cat) {
            const QVariantMap it = v.toMap();
            if (!it.value(QStringLiteral("available")).toBool()) continue;
            const QString k = it.value(QStringLiteral("kind")).toString();
            const QString pdf = app.createReport(pid, k, false, 30, it.value(QStringLiteral("options")).toMap());
            if (head(pdf, 5) != "%PDF-") { check(false, "Berichte: PDF"); qWarning("PDF fehlt: %s", qPrintable(k)); }
            else ++made;
            if (it.value(QStringLiteral("xlsx")).toBool()) {
                const QString x = app.createReport(pid, k, true, 30, it.value(QStringLiteral("options")).toMap());
                if (head(x, 2) != "PK") { check(false, "Berichte: Excel"); qWarning("Excel fehlt: %s", qPrintable(k)); }
                else ++made;
            }
        }
        qInfo("Berichte: %d Dateien erzeugt", made);
        check(made >= 10, "Berichte: alle Ausgaben");
        const QVariantMap s1 = app.shareReports(pid, { QVariantMap{ { QStringLiteral("kind"), QStringLiteral("sheet") } },
                                                      QVariantMap{ { QStringLiteral("kind"), QStringLiteral("journal") }, { QStringLiteral("xlsx"), true } } },
                                                { { QStringLiteral("days"), 7 } });
        check(s1.value(QStringLiteral("ok")).toBool() && app.lastShared().size() == 2 && app.lastShared().value(1).endsWith(QStringLiteral(".xlsx")), "Berichte: zwei teilen");
        check(!app.shareReports(pid, { QVariantMap{ { QStringLiteral("kind"), QStringLiteral("sheet") } } },
                                { { QStringLiteral("zip"), true }, { QStringLiteral("password"), QStringLiteral("kurz") } }).value(QStringLiteral("ok")).toBool(), "Berichte: Passwort zu kurz");
        const QVariantMap s2 = app.shareReports(pid, { QVariantMap{ { QStringLiteral("kind"), QStringLiteral("pharmacy") } },
                                                      QVariantMap{ { QStringLiteral("kind"), QStringLiteral("medplan") } } },
                                                { { QStringLiteral("zip"), true }, { QStringLiteral("password"), QStringLiteral("Geheim-Passwort-1") } });
        check(s2.value(QStringLiteral("ok")).toBool() && app.lastShared().size() == 1 && head(app.lastShared().value(0), 2) == "PK", "Berichte: verschlüsseltes ZIP");
        check(!app.shareReports(pid, {}, {}).value(QStringLiteral("ok")).toBool(), "Berichte: nichts gewählt");

        // Website vom Tablet aus aktualisieren (Testserver: VP_AUTOPILOT_WEB, frische Kopie der Website)
        const QString webUrl = qEnvironmentVariable("VP_AUTOPILOT_WEB");
        const QString key = WebSync::generateUploadKey();
        QVariantMap wc{ { QStringLiteral("enabled"), true }, { QStringLiteral("url"), webUrl.isEmpty() ? QStringLiteral("https://example.invalid/") : webUrl },
                        { QStringLiteral("uploadKey"), key }, { QStringLiteral("password"), QStringLiteral("Website-Passwort-1") },
                        { QStringLiteral("repeat"), QStringLiteral("anders") }, { QStringLiteral("afterSync"), false } };
        check(!app.saveWebsiteConfig(wc), "Website: Passwörter verschieden");
        wc.insert(QStringLiteral("repeat"), QStringLiteral("Website-Passwort-1"));
        check(app.saveWebsiteConfig(wc) && app.websiteConfig().value(QStringLiteral("ready")).toBool(), "Website: Einstellungen");
        if (!webUrl.isEmpty()) {
            check(app.publishWebsiteBlocking(180000), "Website: aktualisiert");
            qInfo("Website: %s", qPrintable(app.websiteConfig().value(QStringLiteral("lastResult")).toString()));
            check(!app.websiteConfig().value(QStringLiteral("last")).toString().isEmpty(), "Website: Zeitpunkt");
        } else {
            QString err;
            const WebSync::Bundle b = WebSync::buildBundle(WebSync::config(), &err);
            check(b.persons >= 1 && b.files.contains(QStringLiteral("data.vpw")), "Website: Paket");
        }

        wait(3600);                           // Hinweis „Website aktualisiert“ ausblenden lassen
        QQmlProperty::write(h, QStringLiteral("tab"), 8);
        wait(600);
        if (QObject *rp = win->findChild<QObject *>(QStringLiteral("reportsPage"))) {
            QQmlProperty::write(rp, QStringLiteral("selected"), QVariantList{ QStringLiteral("sheet"), QStringLiteral("medplan"), QStringLiteral("journal:xlsx") });
            wait(300);
            shot(QStringLiteral("m80_berichte"));
            if (QObject *z = win->findChild<QObject *>(QStringLiteral("reportsZip"))) QQmlProperty::write(z, QStringLiteral("checked"), true);
            wait(400);
            QQmlProperty::write(rp, QStringLiteral("contentY"), std::max(0.0, QQmlProperty::read(rp, QStringLiteral("contentHeight")).toDouble()
                                                                            - QQmlProperty::read(rp, QStringLiteral("height")).toDouble()));
            wait(300);
            shot(QStringLiteral("m81_berichte_teilen"));
            QQmlProperty::write(rp, QStringLiteral("selected"), QVariantList{});
        } else check(false, "Berichte: Seite");
        QQmlProperty::write(h, QStringLiteral("tab"), 0);
        QMetaObject::invokeMethod(h, "openSettings");
        wait(600);
        if (QObject *we = win->findChild<QObject *>(QStringLiteral("webEnabled"))) {
            if (auto *it = qobject_cast<QQuickItem *>(we)) {
                for (QQuickItem *p = it->parentItem(); p; p = p->parentItem())
                    if (p->inherits("QQuickFlickable")) {
                        QQmlProperty::write(p, QStringLiteral("contentY"), std::max(0.0, it->mapToItem(p->childItems().value(0), QPointF()).y() - 120));
                        break;
                    }
            }
            // für das Bildschirmfoto: Beispieladresse statt Testserver, Schlüssel gekürzt
            for (QQuickItem *f = nullptr; !f;) {
                f = qobject_cast<QQuickItem *>(win->contentItem());
                std::function<void(QQuickItem *)> walk = [&](QQuickItem *i) {
                    for (QQuickItem *c : i->childItems()) {
                        const QString t = QQmlProperty::read(c, QStringLiteral("text")).toString();
                        if (c->inherits("QQuickTextField") && t == webUrl) QQmlProperty::write(c, QStringLiteral("text"), QStringLiteral("https://vitaparms.example.de/"));
                        else if (c->inherits("QQuickTextField") && t == key) QQmlProperty::write(c, QStringLiteral("text"), key.left(6) + QStringLiteral("…"));
                        walk(c);
                    }
                };
                walk(f);
            }
            wait(300);
            shot(QStringLiteral("m82_website"));
        } else check(false, "Website: Einstellungen auf der Seite");
        stackPop();
        // Website wieder abschalten (die Demo-Datenbank soll nicht ins Leere hochladen)
        wc.insert(QStringLiteral("enabled"), false);
        app.saveWebsiteConfig(wc);
    }

    // ---- Verwaltung: Konten, Passwort, Wiederherstellung, Sicherung, Demo (Stufe 6)
    {
        const QString self = app.userName();
        const QString selfPw = qEnvironmentVariable("VP_AUTOPILOT_PASSWORD");
        auto names = [&] { QStringList l; for (const QVariant &v : app.accountList()) l << v.toMap().value(QStringLiteral("name")).toString(); return l; };
        check(names().contains(self, Qt::CaseInsensitive), "Verwaltung: Kontenliste");
        check(!app.addAccount(QStringLiteral("pflege"), QStringLiteral("Pflege-Test-1"), QStringLiteral("anders"), false), "Verwaltung: Passwörter verschieden");
        check(app.addAccount(QStringLiteral("pflege"), QStringLiteral("Pflege-Test-1"), QStringLiteral("Pflege-Test-1"), false)
              && names().contains(QStringLiteral("pflege")), "Verwaltung: Konto anlegen");
        check(!app.addAccount(QStringLiteral("pflege"), QStringLiteral("Pflege-Test-1"), QStringLiteral("Pflege-Test-1"), false), "Verwaltung: Konto doppelt");
        {
            QByteArray k; Role r;
            check(Session::keyStore().unlock(QStringLiteral("pflege"), QStringLiteral("Pflege-Test-1"), &k, &r) && r == Role::User, "Verwaltung: neues Konto entsperrt");
        }
        check(app.setAccountAdmin(QStringLiteral("pflege"), true) && app.setAccountAdmin(QStringLiteral("pflege"), false), "Verwaltung: Rolle");
        check(app.resetAccountPassword(QStringLiteral("pflege"), QStringLiteral("Pflege-Neu-22"), QStringLiteral("Pflege-Neu-22")), "Verwaltung: Passwort neu setzen");
        {
            QByteArray k; Role r;
            check(Session::keyStore().unlock(QStringLiteral("pflege"), QStringLiteral("Pflege-Neu-22"), &k, &r), "Verwaltung: neues Passwort gilt");
            KeyStore disk;
            check(disk.load(Session::keyStore().path()) && disk.find(QStringLiteral("pflege")), "Verwaltung: Schlüsseldatei gespeichert");
        }
        check(!app.removeAccount(self), "Verwaltung: eigenes Konto bleibt");
        check(!app.changeOwnPassword(QStringLiteral("falsch"), QStringLiteral("Neues-Passwort-1"), QStringLiteral("Neues-Passwort-1")), "Verwaltung: altes Passwort geprüft");
        check(app.changeOwnPassword(selfPw, QStringLiteral("Neues-Passwort-1"), QStringLiteral("Neues-Passwort-1"))
              && app.changeOwnPassword(QStringLiteral("Neues-Passwort-1"), selfPw, selfPw), "Verwaltung: eigenes Passwort ändern");
        const QString code = app.recoveryCode();
        check(code.size() > 20 && !KeyStore::keyFromRecoveryCode(code).isEmpty(), "Verwaltung: Wiederherstellungsschlüssel");
        { QFile f(app.recoveryCodePdf()); check(f.open(QIODevice::ReadOnly) && f.read(5) == "%PDF-", "Verwaltung: Schlüssel als PDF"); }
        const QString backup = app.createBackup();
        check(!backup.isEmpty() && DbTransfer::isBundle(backup), "Verwaltung: Sicherung");
        check(app.shareBackup() && app.lastShared().value(0).endsWith(QStringLiteral(".vpdb")), "Verwaltung: Sicherung teilen");

        // Bildschirmfotos der Verwaltung
        QMetaObject::invokeMethod(h, "openAdmin");
        wait(600);
        if (QObject *ap = win->findChild<QObject *>(QStringLiteral("adminPage"))) {
            shot(QStringLiteral("m90_verwaltung"));
            QQmlProperty::write(ap, QStringLiteral("code"), code);
            if (QObject *fl = win->findChild<QObject *>(QStringLiteral("adminFlick"))) {
                wait(300);
                QQmlProperty::write(fl, QStringLiteral("contentY"), std::max(0.0, QQmlProperty::read(fl, QStringLiteral("contentHeight")).toDouble()
                                                                                  - QQmlProperty::read(fl, QStringLiteral("height")).toDouble() - 300));
            }
            wait(400);
            shot(QStringLiteral("m91_verwaltung_sicherung"));
        } else check(false, "Verwaltung: Seite");
        stackPop();
        check(app.removeAccount(QStringLiteral("pflege")) && !names().contains(QStringLiteral("pflege")), "Verwaltung: Konto löschen");

        // Passwort vergessen: sperren, mit dem Wiederherstellungsschlüssel wieder hinein
        app.lock();
        QMetaObject::invokeMethod(win, "goStart");
        wait(600);
        if (QObject *lp = win->findChild<QObject *>(QStringLiteral("loginPage"))) {
            if (QObject *rp = win->findChild<QObject *>(QStringLiteral("recoverPopup"))) {
                QMetaObject::invokeMethod(rp, "open");
                wait(300);
                if (QObject *f = win->findChild<QObject *>(QStringLiteral("recoveryCodeField"))) QQmlProperty::write(f, QStringLiteral("text"), code);
                wait(300);
                shot(QStringLiteral("m92_passwort_vergessen"));
                QMetaObject::invokeMethod(rp, "close");
            } else check(false, "Wiederherstellung: Dialog");
            Q_UNUSED(lp)
        } else check(false, "Wiederherstellung: Anmeldeseite");
        check(!app.recoverAccess(code.left(code.size() - 3) + QStringLiteral("XYZ"), self, selfPw, selfPw), "Wiederherstellung: Tippfehler erkannt");
        check(app.recoverAccess(code, self, selfPw, selfPw) && app.loggedIn() && app.isAdmin(), "Wiederherstellung: entsperrt");
        // Demo-Datenbank auf dem Tablet
        check(app.startDemo() && app.isDemo() && app.loggedIn(), "Demo: gestartet");
        bool erika = false;
        for (const QVariant &v : app.persons()) erika = erika || v.toMap().value(QStringLiteral("name")).toString().contains(QStringLiteral("Mustermann"));
        check(erika && app.journal(app.persons().value(0).toMap().value(QStringLiteral("id")).toLongLong()).size() > 100, "Demo: Daten erzeugt");
        QMetaObject::invokeMethod(win, "goStart");
        wait(900);
        shot(QStringLiteral("m93_demo"));
        app.stopDemo();
        check(!app.isDemo() && !app.loggedIn(), "Demo: beendet");
        check(app.login(self, selfPw) && !app.isDemo(), "Demo: eigene Datenbank wieder");
        QMetaObject::invokeMethod(win, "goStart");
        wait(800);
        h = home();
        if (!h) { qWarning("HomePage nach der Demo nicht gefunden"); return fails + 1; }
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
