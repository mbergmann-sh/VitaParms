// SPDX-License-Identifier: MIT
#include "demodata.h"
#include "fluid.h"
#include "contacts.h"
#include "appsettings.h"
#include "database.h"
#include "medschedule.h"
#include "pdftools.h"
#include "session.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPdfWriter>
#if QT_CONFIG(process)
#include <QProcess>
#endif
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>

namespace {

bool g_active = false;

// Normalverteiltes Rauschen (Box-Muller) aus einem festen Startwert – die Demo sieht
// bei jedem Start gleich aus, nur zeitlich bis heute verschoben.
struct Noise {
    QRandomGenerator rng{ 4711 };
    double uniform() { return rng.generateDouble(); }
    bool chance(double p) { return uniform() < p; }
    double gauss(double sigma)
    {
        const double u1 = std::max(1e-12, uniform()), u2 = uniform();
        return sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
    }
    int minutes(int spread) { return int(std::lround(gauss(spread))); }
};

// weicher Übergang von 0 nach 1 zwischen Tag a und b
double ramp(int day, int a, int b)
{
    if (day <= a) return 0;
    if (day >= b) return 1;
    const double x = double(day - a) / (b - a);
    return x * x * (3 - 2 * x);
}

QByteArray makePdf(const QString &title, const QString &issuer, const QDate &date, const QString &patient,
                   const QStringList &lines, QString *fullText)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    {
        QPdfWriter w(&buf);
        w.setResolution(150);
        w.setPageSize(QPageSize(QPageSize::A4));
        w.setPageMargins(QMarginsF(20, 18, 20, 18), QPageLayout::Millimeter);
        w.setTitle(title);
        w.setCreator(QStringLiteral("VitaParms Demo"));
        QPainter p(&w);
        const QRectF page(QPointF(0, 0), w.pageLayout().paintRectPixels(w.resolution()).size());
        QFont f(QStringLiteral("Helvetica"));
        f.setPointSizeF(9);
        p.setFont(f);
        p.setPen(QColor(0x55, 0x55, 0x55));
        p.drawText(QRectF(page.left(), page.top(), page.width(), 40), Qt::AlignLeft | Qt::AlignTop, issuer);
        p.drawText(QRectF(page.left(), page.top(), page.width(), 40), Qt::AlignRight | Qt::AlignTop,
                   QLocale().toString(date, QStringLiteral("dd.MM.yyyy")));
        p.setPen(QColor(0x1F, 0x78, 0xB9));
        p.drawLine(QPointF(page.left(), page.top() + 44), QPointF(page.right(), page.top() + 44));
        QFont tf = f;
        tf.setPointSizeF(15);
        tf.setBold(true);
        p.setFont(tf);
        p.setPen(Qt::black);
        p.drawText(QRectF(page.left(), page.top() + 70, page.width(), 60), Qt::AlignLeft, title);
        p.setFont(f);
        p.drawText(QRectF(page.left(), page.top() + 130, page.width(), 30), Qt::AlignLeft, patient);
        QFont bf = f;
        bf.setPointSizeF(10.5);
        p.setFont(bf);
        const QFontMetricsF fm(bf, &w);
        qreal y = page.top() + 190;
        for (const QString &l : lines) {
            const QRectF r = fm.boundingRect(QRectF(page.left(), y, page.width(), 2000), Qt::TextWordWrap, l);
            p.drawText(QRectF(page.left(), y, page.width(), r.height() + 4), Qt::TextWordWrap, l);
            y += r.height() + fm.height() * 0.45;
        }
        p.setPen(QColor(0x99, 0x99, 0x99));
        f.setPointSizeF(7.5);
        p.setFont(f);
        p.drawText(QRectF(page.left(), page.bottom() - 30, page.width(), 30), Qt::AlignCenter,
                   DemoData::tr("Beispieldokument der VitaParms-Demo – keine echten Befunddaten"));
    }
    if (fullText) *fullText = (QStringList{ issuer, title, patient } + lines).join(QLatin1Char('\n'));
    return bytes;
}

} // namespace

bool DemoData::isActive() { return g_active; }
void DemoData::setActive(bool on) { g_active = on; }

void DemoData::enterDemoSettings()
{
    QSettings own;                 // eigene Einstellungen (noch unter "VitaParms")
    QCoreApplication::setApplicationName(QStringLiteral("VitaParms-Demo"));
    QSettings demo;
    demo.clear();
    // Sprache, Erscheinungsbild, Einheiten, Messzeitpunkte, Sprachausgabe … übernehmen;
    // nicht: Datenbank, Personen-IDs, Konten, E-Mail-Zugang, Pfade
    static const QStringList skip = { QStringLiteral("database/"), QStringLiteral("users/"), QStringLiteral("login/"),
                                      QStringLiteral("smtp/"), QStringLiteral("mail/"), QStringLiteral("paths/"),
                                      QStringLiteral("security/") };
    for (const QString &k : own.allKeys()) {
        bool keep = true;
        for (const QString &s : skip)
            if (k.startsWith(s, Qt::CaseInsensitive)) { keep = false; break; }
        if (keep) demo.setValue(k, own.value(k));
    }
    demo.setValue(QStringLiteral("security/autoLockMinutes"), 0);        // keine Sperre in der Demo
    demo.setValue(QStringLiteral("appointments/showOnStart"), true);
    demo.setValue(QStringLiteral("chart/panels"),
                  QStringList{ QStringLiteral("bp"), QStringLiteral("glucose"), QStringLiteral("weight") });
    demo.setValue(QStringLiteral("chart/scale"), 3);                      // Jahresverlauf
    demo.remove(QStringLiteral("chart/hidden"));
    setActive(true);
}

QString DemoData::databasePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("vitaparms-demo.sqlite"));
}

bool DemoData::launch()
{
#if QT_CONFIG(process) && !defined(VP_MOBILE)
    return QProcess::startDetached(QCoreApplication::applicationFilePath(), { QStringLiteral("--demo") });
#else
    return false;      // Tablet: die App wechselt selbst in die Demo (AppController::startDemo)
#endif
}

bool DemoData::create(const QString &path, qint64 *patientId, QString *error,
                      const std::function<void(int)> &progress)
{
    Database::instance().close();
    for (const QString &f : { path, path + QStringLiteral("-wal"), path + QStringLiteral("-shm"),
                              path + QStringLiteral("-journal"), path + QStringLiteral(".keys") })
        if (QFile::exists(f) && !QFile::remove(f)) {
            if (error) *error = tr("Die alte Demo-Datenbank %1 lässt sich nicht löschen.").arg(QDir::toNativeSeparators(f));
            return false;
        }
    if (!Session::createDatabase(path, accountName(), password(), nullptr, error))
        return false;
    return populate(patientId, QDate::currentDate(), error, progress);
}

bool DemoData::populate(qint64 *patientId, const QDate &today, QString *error,
                        const std::function<void(int)> &progress)
{
    Database &db = Database::instance();
    const QDate d0 = today.addDays(-(kDays - 1));
    const QDateTime now = QDateTime::currentDateTime();
    const QString owner = Session::user().isEmpty() ? accountName() : Session::user();
    auto report = [&](int pct) { if (progress) progress(pct); };

    // ------------------------------------------------------------- Stammblatt
    Patient p;
    p.lastName = QStringLiteral("Mustermann");
    p.firstName = QStringLiteral("Erika");
    p.birthDate = QDate(1952, 3, 14);
    p.sex = QStringLiteral("w");
    p.street = QStringLiteral("Musterstraße 12");
    p.zip = QStringLiteral("12345");
    p.city = QStringLiteral("Musterstadt");
    p.phone = QStringLiteral("01234 567890");
    p.mobile = QStringLiteral("0170 1234567");
    p.diagnoses = tr("Arterielle Hypertonie (I10.90)\nDiabetes mellitus Typ 2 (E11.90)\n"
                     "Asthma bronchiale (J45.9)\nAdipositas Grad I (E66.0)");
    p.notes = tr("Demo-Person: Alle Angaben sind frei erfunden.");
    p.extra.insert(QStringLiteral("Allergien"), tr("Penicillin (Hautausschlag)"));
    Fluid::setLimits(p, 1500, 0, 0);       // Trinkmenge: mindestens 1,5 l am Tag
    {   // Kontakte: Krankenkasse, Hausärztin, Fachärzte, Klinik, Apotheke, Notfallkontakt
        auto mk = [](int kind, const QString &name, const QString &spec, const QString &person, const QString &street,
                     const QString &city, const QString &phone, const QString &email = {}, const QString &web = {}) {
            Contacts::Contact c;
            c.uid = Contacts::newUid(); c.kind = kind; c.name = name; c.specialty = spec; c.person = person;
            c.street = street; c.zip = city.isEmpty() ? QString() : QStringLiteral("12345"); c.city = city;
            c.phone = phone; c.email = email; c.website = web;
            return c;
        };
        QList<Contacts::Contact> cl;
        Contacts::Contact kk = mk(Contacts::Insurance, tr("Muster-Krankenkasse"), {}, {}, tr("Kassenplatz 1"),
                                  tr("Musterstadt"), QStringLiteral("0800 1234567"), QStringLiteral("service@muster-kk.example"),
                                  QStringLiteral("www.muster-kk.example"));
        kk.insuranceNo = QStringLiteral("M123456789");
        cl << kk
           << mk(Contacts::FamilyDoctor, tr("Dr. med. Anna Sommer"), tr("Allgemeinmedizin"), {}, tr("Lindenweg 3"),
                 tr("Musterstadt"), QStringLiteral("01234 98765"), QStringLiteral("praxis@dr-sommer.example"),
                 QStringLiteral("www.dr-sommer.example"))
           << mk(Contacts::Specialist, tr("Dr. med. Peter Winter"), tr("Kardiologie"), {}, tr("Am Markt 8"),
                 tr("Musterstadt"), QStringLiteral("01234 55660"))
           << mk(Contacts::Specialist, tr("Diabeteszentrum Musterstadt"), tr("Diabetologie"), tr("Diabetesberaterin Frau Klein"),
                 tr("Bahnhofstraße 20"), tr("Musterstadt"), QStringLiteral("01234 77880"))
           << mk(Contacts::Clinic, tr("Klinikum Musterstadt"), tr("Innere Medizin, Station 4b"), {}, tr("Klinikallee 1"),
                 tr("Musterstadt"), QStringLiteral("01234 9000"), {}, QStringLiteral("www.klinikum-musterstadt.example"))
           << mk(Contacts::Pharmacy, tr("Linden-Apotheke"), {}, {}, tr("Lindenweg 5"), tr("Musterstadt"),
                 QStringLiteral("01234 44332"));
        Contacts::Contact em = mk(Contacts::Emergency, QStringLiteral("Max Mustermann"), {}, tr("Ehemann"), {}, {}, {});
        em.mobile = QStringLiteral("0170 7654321");
        cl << em;
        Contacts::store(p, cl);
    }
    p.careLevel = 2;
    p.careReview = QDate::currentDate().addDays(45);
    p.disabilityDegree = 60;
    p.disabilityMarks = QStringList{ QStringLiteral("G"), QStringLiteral("RF") };
    p.disabilityValidUntil = QDate(QDate::currentDate().year() + 3, 12, 31);
    if (!db.savePatient(p, error)) return false;
    if (patientId) *patientId = p.id;
    report(5);

    // ------------------------------------------------------------- Medikamentenplan
    auto med = [&](const QString &name, const QString &subst, const QString &strength, const QString &form,
                   const char *mo, const char *mi, const char *ab, const char *na, const QString &unit,
                   const QString &reason, int startDay, int category = Medication::Regular) {
        Medication m;
        m.name = name; m.substance = subst; m.strength = strength; m.form = form;
        m.morning = QString::fromLatin1(mo); m.noon = QString::fromLatin1(mi);
        m.evening = QString::fromLatin1(ab); m.night = QString::fromLatin1(na);
        m.unit = unit; m.reason = reason; m.category = category;
        m.startDate = d0.addDays(startDay);
        return m;
    };
    const QString tab = tr("Tablette"), ftab = tr("Filmtablette"), pcs = tr("Stück");
    const QString bp = tr("Bluthochdruck"), dm = tr("Diabetes mellitus Typ 2"), asthma = tr("Asthma bronchiale");
    QList<Medication> meds;
    meds << med(QStringLiteral("Ramipril-ratiopharm"), QStringLiteral("Ramipril"), QStringLiteral("5 mg"), tab,
                "1", "0", "0", "0", pcs, bp, -400);
    meds << med(QStringLiteral("Amlodipin AbZ"), tr("Amlodipin"), QStringLiteral("5 mg"), tab,
                "1", "0", "0", "0", pcs, bp, 60);
    Medication metOld = med(QStringLiteral("Metformin Lich"), QStringLiteral("Metformin"), QStringLiteral("500 mg"), ftab,
                            "1", "0", "1", "0", pcs, dm, -300);
    metOld.endDate = d0.addDays(119);
    metOld.archived = true;
    metOld.archivedAt = QDateTime(d0.addDays(120), QTime(10, 15));
    meds << metOld;
    meds << med(QStringLiteral("Metformin Lich"), QStringLiteral("Metformin"), QStringLiteral("1000 mg"), ftab,
                "1", "0", "1", "0", pcs, dm, 120);
    Medication sym = med(QStringLiteral("Symbicort Turbohaler"), tr("Budesonid/Formoterol"),
                         tr("160/4,5 µg/Hub"), tr("Pulver zur Inhalation"), "1", "0", "1", "0", tr("Hub"), asthma, -500);
    sym.notes = tr("nach der Inhalation Mund ausspülen");
    meds << sym;
    meds << med(QStringLiteral("ASS AbZ protect"), tr("Acetylsalicylsäure"), QStringLiteral("100 mg"), tab,
                "0", "1", "0", "0", pcs, tr("Vorbeugung"), -600);
    meds << med(QStringLiteral("Simvastatin Heumann"), QStringLiteral("Simvastatin"), QStringLiteral("20 mg"), ftab,
                "0", "0", "1", "0", pcs, tr("erhöhte Blutfette"), -600);
    Medication vitd = med(QStringLiteral("Dekristol"), tr("Colecalciferol (Vitamin D3)"), tr("20.000 IE"),
                          tr("Weichkapsel"), "1", "", "", "", pcs, tr("Vitamin-D-Mangel"), 0);
    vitd.rhythm.freq = Recurrence::Weekly;
    vitd.rhythm.weekdays = 1 << 6;                   // sonntags
    vitd.rhythmStart = d0.addDays((7 - d0.dayOfWeek()) % 7);
    vitd.rhythmNote = tr("einmal pro Woche, sonntags zum Frühstück");
    meds << vitd;
    Medication salb = med(QStringLiteral("Salbutamol-ratiopharm N"), QStringLiteral("Salbutamol"), tr("100 µg/Hub"),
                          tr("Dosieraerosol"), "", "", "", "", tr("Hub"), tr("Atemnot"), -500, Medication::AsNeeded);
    salb.rhythmNote = tr("1–2 Hübe bei Atemnot, höchstens 8 Hübe am Tag");
    meds << salb;
    Medication para = med(QStringLiteral("Paracetamol-ratiopharm"), QStringLiteral("Paracetamol"), QStringLiteral("500 mg"), tab,
                          "", "", "", "", pcs, tr("Schmerzen, Fieber"), -500, Medication::AsNeeded);
    para.rhythmNote = tr("bei Fieber über 38,5 °C, höchstens 3× täglich");
    meds << para;
    Medication pred = med(QStringLiteral("Prednisolon Galen"), tr("Prednisolon"), QStringLiteral("20 mg"), tab,
                          "", "", "", "", pcs, tr("schwerer Asthmaanfall"), -200, Medication::Emergency);
    pred.rhythmNote = tr("2 Tabletten bei schwerem Asthmaanfall, danach Notruf 112");
    meds << pred;
    meds << med(QStringLiteral("Bepanthen Wund- und Heilsalbe"), tr("Dexpanthenol"), QStringLiteral("5 %"), tr("Salbe"),
                "", "", "", "", QString(), tr("trockene Haut an den Füßen"), -100, Medication::Other);
    if (!db.saveMedications(p.id, meds, {}, error)) return false;
    meds = db.medications(p.id, true);
    MedSchedule::syncAppointments(p.id, meds);
    report(12);

    // ------------------------------------------------------------- Messwerte (365 Tage)
    const QStringList ctx = AppSettings::defaultProfileContexts();
    const QString cFasting = ctx.value(0), cAfterBreakfast = ctx.value(2), cBeforeLunch = ctx.value(3),
                  cAfterLunch = ctx.value(4), cBeforeDinner = ctx.value(5), cAfterDinner = ctx.value(6),
                  cBedtime = ctx.value(7), cComplaints = ctx.value(10);
    Noise n;
    const int infection = 230;                           // Atemwegsinfekt Tag 230–244
    auto inInfection = [&](int day) { return day >= infection && day <= infection + 14; };
    auto fever = [&](int day) {                          // Temperaturverlauf des Infekts
        static const double t[] = { 38.1, 38.7, 38.9, 38.4, 37.9, 37.5, 37.2 };
        const int i = day - infection;
        return (i >= 0 && i < 7) ? t[i] : 36.7;
    };
    QHash<int, QString> notes{
        { 3, tr("Neues Blutdruckmessgerät (Oberarm) in Betrieb genommen") },
        { 21, tr("Spaziergang 45 Minuten, gut vertragen") },
        { 58, tr("Blutdruck seit Wochen zu hoch – Hausarzt verordnet zusätzlich Amlodipin") },
        { 74, tr("Knöchel abends leicht geschwollen (Amlodipin?) – beim nächsten Termin ansprechen") },
        { 118, tr("HbA1c 7,8 % – Metformin wird auf 1000 mg erhöht") },
        { 150, tr("Fußpflege; Haut an den Fersen trocken") },
        { infection, tr("Husten, Halsschmerzen, Fieber seit gestern Abend") },
        { infection + 1, tr("Hausarzt: Bronchitis, Antibiotikum (kein Penicillin) verordnet") },
        { infection + 3, tr("Atemnot nachts, 2 Hübe Salbutamol") },
        { infection + 7, tr("deutlich besser, kein Fieber mehr") },
        { 300, tr("Diabetesschulung begonnen") },
        { 330, tr("Gewicht stabil, Ernährungsumstellung zeigt Wirkung") },
    };
    int hypoDay = 187;                                   // leichte Unterzuckerung nach Gartenarbeit

    if (!db.beginBulk()) { if (error) *error = tr("Transaktion konnte nicht gestartet werden."); return false; }
    auto save = [&](VitalRecord &r) {
        if (r.measuredAt > now || !r.hasAnyValue()) return true;
        r.patientId = p.id;
        return db.saveVital(r, error);
    };

    // Aufnahmewerte
    {
        VitalRecord a;
        a.isAdmission = true;
        a.measuredAt = QDateTime(d0, QTime(9, 0));
        a.systolic = 156; a.diastolic = 95; a.pulse = 78; a.temperature = 36.6; a.spo2 = 97;
        a.peakFlow = 340; a.glucose = 158; a.weight = 84.2; a.height = 164;
        a.observations = tr("Aufnahme in VitaParms");
        if (!save(a)) { db.commitBulk(); return false; }
    }

    for (int day = 0; day < kDays; ++day) {
        const QDate date = d0.addDays(day);
        const bool profileDay = date.dayOfWeek() == 3 || (date.dayOfWeek() == 6 && date.weekNumber() % 2 == 0);
        const bool sick = inInfection(day);
        const double sysBase = 154 - 22 * ramp(day, 60, 100);
        const double diaBase = 94 - 11 * ramp(day, 60, 100);
        const double gluBase = 152 - 30 * ramp(day, 120, 180);
        const bool xmas = (date.month() == 12 && date.day() >= 24) || (date.month() == 1 && date.day() <= 7);
        const double weight = 84.0 - 6.2 * double(day) / kDays + 0.35 * std::sin(day / 9.0)
                            + (xmas ? 1.2 : 0.0) - (sick ? 0.8 : 0.0);
        const int pfBest = int(370 + 15 * ramp(day, 0, 90));

        // Morgens (nüchtern): RR, Puls, Blutzucker, Gewicht, Peak Flow
        VitalRecord m;
        m.measuredAt = QDateTime(date, QTime(7, 10).addSecs(60 * n.minutes(15)));
        m.context = cFasting;
        if (n.chance(0.95)) {
            m.systolic = int(std::lround(sysBase + n.gauss(6) + (sick ? 6 : 0)));
            m.diastolic = int(std::lround(diaBase + n.gauss(4)));
            m.pulse = int(std::lround(74 + n.gauss(5) + (sick ? 14 * std::max(0.0, 1 - (day - infection) / 8.0) : 0)));
        }
        double g = gluBase + n.gauss(9) + (sick ? 18 : 0);
        if (day == hypoDay + 1) g = 96;
        m.glucose = std::round(g);
        if (n.chance(0.85)) m.weight = std::round((weight + n.gauss(0.25)) * 10) / 10;
        double pf = pfBest - 35 + n.gauss(14);
        if (sick) pf -= 90 * std::max(0.0, 1 - (day - infection) / 14.0);
        m.peakFlow = int(std::lround(pf / 10) * 10);
        if (sick || date.dayOfWeek() == 7) {
            m.spo2 = int(std::lround(std::clamp(97 + n.gauss(0.7) - (sick ? 3.5 * std::max(0.0, 1 - (day - infection) / 7.0) : 0), 89.0, 99.0)));
            m.temperature = std::round((sick ? fever(day) : 36.5 + n.gauss(0.15)) * 10) / 10;
        }
        if (sick && day - infection < 6)
            m.extra.insert(QStringLiteral("Atemfrequenz"), int(std::lround(21 - (day - infection) + n.gauss(1))));
        m.observations = notes.value(day);
        // Stuhlgang (eigener Zufall, damit die übrigen Demo-Werte unverändert bleiben): meist täglich
        // Typ 3–4, einzelne harte Tage, unter dem Antibiotikum weich; vor zwei Wochen drei Tage ohne
        const int fromEnd = kDays - 1 - day;
        auto rnd = [day](unsigned salt) { return double(((unsigned(day) * 2654435761u) ^ (salt * 40503u)) % 1000u) / 1000.0; };
        const bool gap = fromEnd >= 15 && fromEnd <= 17;
        const bool antibiotic = day > infection && day - infection <= 8;
        if (!gap && (fromEnd == 14 || rnd(1) < 0.86)) {
            const double t = rnd(2);
            m.stool = antibiotic ? (t < 0.5 ? 6 : t < 0.8 ? 5 : 7)
                    : fromEnd == 14 ? 1
                    : t < 0.08 ? 2 : t < 0.12 ? 0 : t < 0.55 ? 4 : t < 0.9 ? 3 : 5;
        }
        if (!save(m)) { db.commitBulk(); return false; }

        // Tagesprofil Blutzucker (mittwochs und jeden zweiten Samstag)
        if (profileDay) {
            const struct { QTime t; QString c; double add; } prof[] = {
                { QTime(9, 30), cAfterBreakfast, 48 }, { QTime(12, 0), cBeforeLunch, 6 },
                { QTime(14, 30), cAfterLunch, 42 },    { QTime(18, 0), cBeforeDinner, 10 },
                { QTime(20, 30), cAfterDinner, 46 },   { QTime(22, 15), cBedtime, 18 },
            };
            for (const auto &pr : prof) {
                VitalRecord r;
                r.measuredAt = QDateTime(date, pr.t.addSecs(60 * n.minutes(10)));
                r.context = pr.c;
                r.glucose = std::round(gluBase + pr.add * (1 - 0.25 * ramp(day, 120, 200)) + n.gauss(12));
                if (!save(r)) { db.commitBulk(); return false; }
            }
        }

        // Unterzuckerung nach Gartenarbeit
        if (day == hypoDay) {
            VitalRecord h;
            h.measuredAt = QDateTime(date, QTime(16, 40));
            h.context = cComplaints;
            h.glucose = 64; h.pulse = 96;
            h.observations = tr("Zittrig nach Gartenarbeit – Traubenzucker gegessen, nach 15 min 102 mg/dl");
            if (!save(h)) { db.commitBulk(); return false; }
        }

        // Abends: RR, Puls, Peak Flow (bei Infekt auch Temperatur)
        if (n.chance(sick ? 1.0 : 0.8)) {
            VitalRecord e;
            e.measuredAt = QDateTime(date, QTime(19, 40).addSecs(60 * n.minutes(20)));
            e.context = cBeforeDinner;
            e.systolic = int(std::lround(sysBase + 5 + n.gauss(7)));
            e.diastolic = int(std::lround(diaBase + 2 + n.gauss(4)));
            e.pulse = int(std::lround(71 + n.gauss(5) + (sick ? 10 * std::max(0.0, 1 - (day - infection) / 8.0) : 0)));
            double pfe = pfBest - 10 + n.gauss(12);
            if (sick) pfe -= 80 * std::max(0.0, 1 - (day - infection) / 14.0);
            e.peakFlow = int(std::lround(pfe / 10) * 10);
            if (sick && day - infection < 7) e.temperature = std::round((fever(day) + 0.3) * 10) / 10;
            if (day > infection && day - infection <= 5) e.stool = 6;   // Antibiotikum
            if (day >= 74 && day <= 110) {           // Knöchelödeme unter Amlodipin (abends)
                e.edema = day >= 82 && day <= 96 ? 2 : 1;
                e.edemaSite = Fluid::edemaSites().value(0);
                e.edemaSide = Fluid::edemaSides().value(0);
            }
            if (!save(e)) { db.commitBulk(); return false; }
        }
        // Trinkprotokoll der letzten zwei Wochen (eigener Zufall – die übrigen Werte bleiben gleich)
        if (kDays - 1 - day < 14) {
            static const int amounts[] = { 250, 200, 300, 150, 250, 330, 200, 250 };
            const struct { QTime t; int kind; } drinkTimes[] = { { QTime(7, 45), 0 }, { QTime(10, 15), 0 }, { QTime(12, 30), 1 },
                                                           { QTime(15, 0), 0 }, { QTime(18, 15), 0 }, { QTime(20, 30), 0 } };
            int i = 0;
            for (const auto &sl : drinkTimes) {
                if (((unsigned(day) * 2654435761u) ^ (unsigned(i) * 97u)) % 7u == 3u) { ++i; continue; }   // mal vergessen
                VitalRecord f;
                f.measuredAt = QDateTime(date, sl.t);
                f.fluidIn = amounts[(day + i) % 8];
                f.fluidInKind = Fluid::inKinds().value(sl.kind);
                if (!save(f)) { db.commitBulk(); return false; }
                ++i;
            }
        }
        if (day % 20 == 0) report(12 + 70 * day / kDays);
    }

    // Protokoll „Medikamente stellen“: die letzten sieben Tage morgens
    for (int i = 6; i >= 0; --i) {
        MedPreparation mp;
        mp.patientId = p.id;
        mp.forDate = today.addDays(-i);
        mp.preparedAt = QDateTime(mp.forDate, QTime(7, 35));
        if (mp.preparedAt > now) continue;
        mp.preparedBy = owner;
        mp.slot = 0;
        for (const Medication &m : meds)
            if (!m.archived && m.category == Medication::Regular && !m.morning.trimmed().isEmpty()
                && m.morning.trimmed() != QLatin1String("0") && MedSchedule::isDueOn(m, mp.forDate)) {
                QJsonObject it;
                it.insert(QStringLiteral("name"), m.name);
                it.insert(QStringLiteral("strength"), m.strength);
                it.insert(QStringLiteral("form"), m.form);
                it.insert(QStringLiteral("dose"), m.morning + QLatin1Char(' ') + m.unit);
                mp.items.append(it);
            }
        mp.total = mp.confirmed = int(mp.items.size());
        mp.complete = true;
        if (!db.addPreparation(mp, error)) { db.commitBulk(); return false; }
    }
    if (!db.commitBulk()) { if (error) *error = tr("Die Demo-Daten konnten nicht gespeichert werden."); return false; }
    report(85);

    // ------------------------------------------------------------- Termine
    auto appt = [&](const QDateTime &start, int minutes, const QString &title, const QString &location,
                    int prio, const QString &notes, int remind = 60) {
        Appointment a;
        a.patientId = p.id;
        a.owner = owner;
        a.createdBy = owner;
        a.title = title;
        a.location = location;
        a.start = start;
        a.end = start.addSecs(60 * minutes);
        a.priority = prio;
        a.notes = notes;
        a.remindMinutes = remind;
        if (start < now) { a.done = true; a.remindedAt = a.reminderTime(); }
        return a;
    };
    const QString practice = tr("Praxis Dr. Sommer, Musterstadt");
    const QTime soon = QTime(std::clamp(now.time().hour() + 2, 8, 17), 0);
    QList<Appointment> list{
        appt(QDateTime(d0.addDays(58), QTime(9, 30)), 30, tr("Hausarzt: Blutdruckkontrolle"), practice, Appointment::Normal, {}),
        appt(QDateTime(d0.addDays(118), QTime(8, 0)), 15, tr("Labor: Blutabnahme (HbA1c)"), practice, Appointment::Normal, tr("nüchtern erscheinen")),
        appt(QDateTime(d0.addDays(infection + 1), QTime(10, 0)), 20, tr("Hausarzt: Husten und Fieber"), practice, Appointment::High, {}),
        appt(QDateTime(d0.addDays(infection + 16), QTime(11, 0)), 45, tr("Lungenfacharzt: Lungenfunktion"),
             tr("Pneumologie am Markt, Musterstadt"), Appointment::Normal, tr("Peak-Flow-Werte mitbringen")),
        appt(QDateTime(today, soon), 30, tr("Hausarzt: Kontrolle Blutdruck und Blutzucker"), practice, Appointment::High,
             tr("Blutzucker-Tagebuch und Medikamentenplan mitnehmen")),
        appt(QDateTime(today.addDays(1), QTime(8, 0)), 15, tr("Labor: Blutabnahme"), practice, Appointment::Normal,
             tr("nüchtern erscheinen, Versichertenkarte mitnehmen")),
        appt(QDateTime(today.addDays(4), QTime(15, 30)), 60, tr("Diabetesschulung, Teil 4"),
             tr("Diabeteszentrum Musterstadt"), Appointment::Normal, {}),
        appt(QDateTime(today.addDays(12), QTime(10, 15)), 30, tr("Augenarzt: Netzhautkontrolle (Diabetes)"),
             tr("Augenpraxis Lindenallee"), Appointment::Normal, tr("Pupillen werden erweitert – nicht selbst fahren"), 24 * 60),
        appt(QDateTime(today.addDays(26), QTime(9, 0)), 20, tr("Rezept abholen: Metformin, Symbicort"), practice, Appointment::Low, {}),
    };
    for (Appointment &a : list)
        if (!db.saveAppointment(a, error)) return false;
    // Physiotherapie: zehn Termine dienstags und freitags
    {
        QDate first = today.addDays(1);
        while (first.dayOfWeek() != 2 && first.dayOfWeek() != 5) first = first.addDays(1);
        Appointment a = appt(QDateTime(first, QTime(14, 0)), 30, tr("Physiotherapie: Atemtherapie"),
                             tr("Physiopraxis Bewegung, Musterstadt"), Appointment::Normal, tr("Handtuch mitbringen"), 120);
        Recurrence r;
        r.freq = Recurrence::Weekly;
        r.weekdays = (1 << 1) | (1 << 4);
        r.count = 10;
        if (!db.createSeries(a, r, error)) return false;
    }
    report(92);

    // ------------------------------------------------------------- Krankenakte
    const QString pat = tr("Patientin: Erika Mustermann, geb. 14.03.1952");
    const QStringList cats = PdfTools::defaultCategories();
    struct Doc { int day; QString cat, title, issuer; QStringList lines; };
    const QList<Doc> docs{
        { 100, cats.value(1), tr("Langzeit-Blutdruckmessung (24 h)"), tr("Praxis Dr. med. Anna Sommer"),
          { tr("Mittelwert 24 h: 138/86 mmHg, Tag 142/89 mmHg, Nacht 127/78 mmHg."),
            tr("Nächtliche Absenkung 11 % (normal: 10–20 %)."),
            tr("Beurteilung: unter Ramipril und Amlodipin deutlich gebesserte, aber noch nicht optimale Einstellung."),
            tr("Empfehlung: Fortführung der Therapie, Kontrolle in drei Monaten.") } },
        { 118, cats.value(2), tr("Laborbefund"), tr("Labor Musterstadt"),
          { tr("HbA1c: 7,8 % (Zielbereich individuell < 7,5 %)"), tr("Nüchternglukose: 151 mg/dl"),
            tr("Kreatinin: 0,9 mg/dl, eGFR 68 ml/min/1,73 m²"), tr("LDL-Cholesterin: 104 mg/dl"),
            tr("Kalium: 4,4 mmol/l") } },
        { infection + 16, cats.value(0), tr("Arztbrief Pneumologie"), tr("Pneumologie am Markt, Dr. med. Jonas Berg"),
          { tr("Vorstellung nach infektgetriggerter Asthma-Exazerbation."),
            tr("Lungenfunktion: FEV1 82 % des Sollwerts, nach Bronchospasmolyse 91 %."),
            tr("Peak-Flow-Protokoll mit deutlichem Abfall während des Infekts, inzwischen wieder im persönlichen Bestbereich."),
            tr("Therapie: Symbicort 160/4,5 µg 2× täglich fortführen, Salbutamol bei Bedarf. Kontrolle in sechs Monaten.") } },
        { kDays - 15, cats.value(2), tr("Laborbefund"), tr("Labor Musterstadt"),
          { tr("HbA1c: 6,9 %"), tr("Nüchternglukose: 124 mg/dl"),
            tr("Kreatinin: 0,9 mg/dl, eGFR 70 ml/min/1,73 m²"), tr("LDL-Cholesterin: 92 mg/dl") } },
    };
    for (const Doc &d : docs) {
        MedicalDocument md;
        md.patientId = p.id;
        md.docDate = d0.addDays(d.day);
        md.category = d.cat;
        md.title = d.title;
        md.issuer = d.issuer;
        md.fileName = d.title + QStringLiteral(" ") + md.docDate.toString(QStringLiteral("yyyy-MM-dd")) + QStringLiteral(".pdf");
        const QByteArray pdf = makePdf(d.title, d.issuer, md.docDate, pat, d.lines, &md.fullText);
        md.sha256 = QCryptographicHash::hash(pdf, QCryptographicHash::Sha256);
        md.size = pdf.size();
        md.pages = 1;
        md.addedAt = QDateTime(md.docDate.addDays(2), QTime(17, 0));
        if (!db.addDocument(md, pdf, error)) return false;
    }
    report(100);
    return true;
}
