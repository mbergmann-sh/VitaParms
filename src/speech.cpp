// SPDX-License-Identifier: MIT
#include "speech.h"

#include <algorithm>

#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>

#ifdef VP_HAVE_TTS
#include <QTextToSpeech>
#include <QVoice>
#endif

#ifdef VP_HAVE_TTS
namespace {
// Eine einzige Engine für das ganze Programm: Einige Engines (z. B. flite) geben beim
// Zerstören globale Stimmdaten frei, während eine zweite Instanz noch spricht → Absturz.
QTextToSpeech *sharedEngine()
{
    static QTextToSpeech *engine = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        if (!QTextToSpeech::availableEngines().isEmpty()) {
            engine = new QTextToSpeech(QCoreApplication::instance());
            // Android startet die Engine asynchron: bis dahin meldet sie „Error“ ohne Ursache
            if (engine->state() == QTextToSpeech::Error && engine->errorReason() != QTextToSpeech::ErrorReason::NoError) {
                delete engine;
                engine = nullptr;
            }
        }
    }
    return engine;
}
const Speech *g_speaker = nullptr;   // wer zuletzt gesprochen hat
} // namespace
#endif

Speech::Speech(QObject *parent) : QObject(parent)
{
#ifdef VP_HAVE_TTS
    m_tts = sharedEngine();
    if (m_tts) {
        connect(m_tts, &QTextToSpeech::stateChanged, this, [this](QTextToSpeech::State s) {
            // Android: die Engine startet asynchron – Stimme erst wählen, wenn sie bereit ist,
            // und einen in der Zwischenzeit angeforderten Text dann vorlesen
            if (s == QTextToSpeech::Ready && !m_pending.isEmpty()) {
                const QString t = m_pending;
                m_pending.clear();
                chooseVoice();
                say(t);
                return;
            }
            if (s == QTextToSpeech::Ready && m_desc.isEmpty()) chooseVoice();
            if (g_speaker != this) return;
            if (s == QTextToSpeech::Speaking) m_speaking = true;
            else if (m_speaking && (s == QTextToSpeech::Ready || s == QTextToSpeech::Error)) {
                m_speaking = false;
                emit finished();
            }
        });
        chooseVoice();
    }
#endif
}

Speech::~Speech()
{
#ifdef VP_HAVE_TTS
    if (g_speaker == this) {
        if (m_tts) m_tts->stop();
        g_speaker = nullptr;
    }
#endif
}

bool Speech::available() const
{
#ifdef VP_HAVE_TTS
    return m_tts != nullptr
           && (m_tts->state() != QTextToSpeech::Error || m_tts->errorReason() == QTextToSpeech::ErrorReason::NoError);
#else
    return false;
#endif
}

bool Speech::isSpeaking() const
{
    return m_speaking;
}

void Speech::setChoice(const QString &choice)
{
    m_choice = choice.isEmpty() ? QStringLiteral("female") : choice;
    chooseVoice();
}

QString Speech::hint() const
{
    return m_hint;
}

void Speech::setRate(double rate)
{
#ifdef VP_HAVE_TTS
    m_rate = qBound(-1.0, rate, 1.0);
    if (m_tts) m_tts->setRate(m_rate);
#else
    Q_UNUSED(rate)
#endif
}

bool Speech::languageMatches() const
{
#ifdef VP_HAVE_TTS
    return m_tts && m_tts->locale().language() == QLocale().language();
#else
    return false;
#endif
}

QString Speech::voiceDescription() const
{
    return m_desc;
}

int Speech::guessGender(const QString &voiceName, int engineGender)
{
#ifdef VP_HAVE_TTS
    // Bekannte Stimmen der Betriebssysteme und Engines; sonst Angabe der Engine.
    static const char *const female[] = {
        "female", "frau", "woman", "slt", "hedda", "katja", "zira", "hazel", "susan", "heera", "eva",
        "anna", "petra", "helena", "marlene", "vicki", "samantha", "karen", "moira", "tessa", "victoria",
        "allison", "ava", "serena", "kate", "fiona", "veena", "julia", "claudia", "lisa", "sabina", "clb", "lea" };
    static const char *const male[] = {
        "male", "mann", "rms", "kal", "awb", "stefan", "david", "mark", "george", "markus", "yannick",
        "hans", "daniel", "alex", "fred", "tom", "oliver", "james", "richard", "ravi", "conrad", "bdl" };
    const QString n = voiceName.toLower();
    auto word = [&n](const char *w) {
        const QString k = QLatin1String(w);
        // als eigenes Wort bzw. Wortanfang, damit z. B. "Annabelle" nicht als "Anna" gilt
        static const QRegularExpression sep(QStringLiteral("[^a-z0-9]+"));
        for (const QString &part : n.split(sep, Qt::SkipEmptyParts)) {
            if (part == k) return true;
            if (k.size() >= 4 && part.startsWith(k) && part.size() <= k.size() + 2) return true;   // male1, female2
        }
        return false;
    };
    for (const char *w : female) if (word(w)) return QVoice::Female;   // zuerst: "female" enthält "male"
    for (const char *w : male) if (word(w)) return QVoice::Male;
    return engineGender;
#else
    Q_UNUSED(voiceName)
    return engineGender;
#endif
}

#ifdef VP_HAVE_TTS
namespace {
struct VoiceEntry { QLocale locale; QVoice voice; int gender; };

// Alle Stimmen aller Sprachen (einmalig ermittelt; die Engine wird dabei kurz umgestellt)
const QList<VoiceEntry> &allVoices(QTextToSpeech *tts)
{
    static QList<VoiceEntry> cache;
    static bool done = false;
    if (done || !tts) return cache;
    if (tts->state() != QTextToSpeech::Ready) return cache;   // Engine startet noch: später erneut
    done = true;
    const QLocale keep = tts->locale();
    const QVoice keepVoice = tts->voice();
    for (const QLocale &l : tts->availableLocales()) {
#ifdef Q_OS_ANDROID
        // Android-Engines kennen sehr viele Sprachen; nur Oberflächensprache, Deutsch und Englisch abfragen
        if (l.language() != QLocale().language() && l.language() != QLocale::German && l.language() != QLocale::English) continue;
#endif
        tts->setLocale(l);
        for (const QVoice &v : tts->availableVoices())
            cache << VoiceEntry{ l, v, Speech::guessGender(v.name(), v.gender()) };
    }
    tts->setLocale(keep);
    tts->setVoice(keepVoice);
    return cache;
}
QString key(const VoiceEntry &e) { return QStringLiteral("voice:") + e.locale.name() + QLatin1Char('|') + e.voice.name(); }
} // namespace
#endif

QList<QPair<QString, QString>> Speech::voiceChoices()
{
    QList<QPair<QString, QString>> l;
    l << qMakePair(QStringLiteral("female"), QCoreApplication::translate("Speech", "weiblich (automatisch)"))
      << qMakePair(QStringLiteral("male"), QCoreApplication::translate("Speech", "männlich (automatisch)"));
#ifdef VP_HAVE_TTS
    Speech probe;   // stellt die gemeinsame Engine bereit
    if (!probe.m_tts) return l;
    QList<VoiceEntry> v = allVoices(probe.m_tts);
    const auto lang = QLocale().language();
    std::stable_sort(v.begin(), v.end(), [lang](const VoiceEntry &a, const VoiceEntry &b) {
        return (a.locale.language() == lang) > (b.locale.language() == lang);   // Oberflächensprache zuerst
    });
    for (const VoiceEntry &e : v) {
        const QString g = e.gender == QVoice::Male ? QCoreApplication::translate("Speech", "männlich")
                        : e.gender == QVoice::Female ? QCoreApplication::translate("Speech", "weiblich")
                                                     : QCoreApplication::translate("Speech", "unbekannt");
        l << qMakePair(key(e), QStringLiteral("%1 – %2, %3").arg(e.voice.name(),
                                                                QLocale::languageToString(e.locale.language()), g));
    }
#endif
    return l;
}

void Speech::chooseVoice()
{
#ifdef VP_HAVE_TTS
    if (!m_tts) return;
    m_hint.clear();
    const QList<VoiceEntry> &all = allVoices(m_tts);
    auto apply = [this](const VoiceEntry &e, double pitch) {
        m_tts->setLocale(e.locale);
        m_tts->setVoice(e.voice);
        m_tts->setPitch(pitch);
        m_desc = e.voice.name();
    };
    // 1) bestimmte Stimme
    if (m_choice.startsWith(QLatin1String("voice:"))) {
        for (const VoiceEntry &e : all)
            if (key(e) == m_choice) { apply(e, 0.0); return; }
        m_hint = QCoreApplication::translate("Speech", "Die gewählte Stimme ist nicht mehr installiert – "
                                                       "es wird automatisch gewählt.");
    }
    const bool wantMale = m_choice == QLatin1String("male");
    const int g = wantMale ? QVoice::Male : QVoice::Female;
    // 2) passende Stimme in der Oberflächensprache, sonst in irgendeiner Sprache
    const auto lang = QLocale().language();
    QList<VoiceEntry> same;
    for (const VoiceEntry &e : all)
        if (e.locale.language() == lang) same << e;
    const QList<VoiceEntry> &pool = same.isEmpty() ? all : same;
    for (const VoiceEntry &e : pool)
        if (e.gender == g) { apply(e, 0.0); return; }
    if (pool.isEmpty()) { m_desc.clear(); return; }
    // 3) keine passende Stimme installiert: vorhandene Stimme deutlich tiefer/höher stellen
    apply(pool.first(), wantMale ? -0.6 : 0.35);
    m_desc = pool.first().voice.name() + QCoreApplication::translate("Speech", " (Tonhöhe angepasst)");
    m_hint = (wantMale ? QCoreApplication::translate("Speech", "Keine männliche Stimme für %1 installiert")
                       : QCoreApplication::translate("Speech", "Keine weibliche Stimme für %1 installiert"))
                 .arg(QLocale::languageToString(pool.first().locale.language()))
           + QCoreApplication::translate("Speech", " – die vorhandene Stimme wird nur in der Tonhöhe verändert. "
                                                   "Weitere Stimmen lassen sich in den Sprach-Einstellungen des "
                                                   "Betriebssystems installieren.");
#endif
}

void Speech::say(const QString &text)
{
#ifdef VP_HAVE_TTS
    if (!m_tts) return;
    if (m_tts->state() == QTextToSpeech::Error) {
        // Engine startet noch (Android, ohne Fehlerursache): vorlesen, sobald sie bereit ist
        if (m_tts->errorReason() == QTextToSpeech::ErrorReason::NoError) m_pending = text;
        return;
    }
    m_pending.clear();
    m_tts->stop();
    if (g_speaker != this || m_desc.isEmpty()) chooseVoice();   // gemeinsame Engine: eigene Stimme/Tempo wieder setzen
    g_speaker = this;
    m_speaking = true;
    m_tts->setRate(m_rate);
    m_tts->say(text);
#else
    Q_UNUSED(text)
#endif
}

void Speech::stop()
{
#ifdef VP_HAVE_TTS
    if (m_tts && g_speaker == this) m_tts->stop();
#endif
}

QString Speech::expand(const QString &input)
{
    const bool de = QLocale().language() == QLocale::German;
    QString t = QStringLiteral(" ") + input + QStringLiteral(" ");
    struct R { const char *rx; const char *de; const char *en; };
    static const R rules[] = {
        { "(?<=\\s)1\\s*1/2(?=\\s)", "eineinhalb", "one and a half" },
        { "(?<=\\s)1/2(?=\\s)|½", "eine halbe", "half a" },
        { "(?<=\\s)1/4(?=\\s)|¼", "ein Viertel", "a quarter" },
        { "(?<=\\s)3/4(?=\\s)|¾", "drei Viertel", "three quarters" },
        { "(?<=[\\d\\s])µg(?=[\\s,.;)])|(?<=[\\d\\s])mcg(?=[\\s,.;)])", " Mikrogramm", " micrograms" },
        { "(?<=[\\d\\s])mg(?=[\\s,.;/)])", " Milligramm", " milligrams" },
        { "(?<=[\\d\\s])ml(?=[\\s,.;)])", " Milliliter", " millilitres" },
        { "(?<=[\\d\\s])g(?=[\\s,.;)])", " Gramm", " grams" },
        { "(?<=[\\d\\s])(IE|I\\.E\\.|IU)(?=[\\s,.;)])", " internationale Einheiten", " international units" },
        { "(?<=[\\d\\s])E/ml(?=[\\s,.;)])", " Einheiten pro Milliliter", " units per millilitre" },
        { "/ml(?=[\\s,.;)])", " pro Milliliter", " per millilitre" },
        { "(?<=\\d)\\s?×", " mal", " times" },
        { "(?<=\\s)Stk\\.?(?=\\s)", "Stück", "pieces" },
        { "(?<=\\s)Stück(?=[\\s,.;])", "Stück", "pieces" },
        { "(?<=\\s)Tbl\\.?(?=\\s)", "Tablette", "tablet" },
        { "(?<=\\s)s\\.c\\.(?=[\\s,])", "unter die Haut", "under the skin" },
        { "(?<=\\s)i\\.m\\.(?=[\\s,])", "in den Muskel", "into the muscle" },
        { "(?<=\\s)z\\. ?B\\.(?=\\s)", "zum Beispiel", "for example" },
    };
    for (const R &r : rules)
        t.replace(QRegularExpression(QString::fromUtf8(r.rx)), QString::fromUtf8(de ? r.de : r.en));
    return t.simplified();
}
