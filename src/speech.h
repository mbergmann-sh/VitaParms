// VitaParms – Sprachausgabe (Qt TextToSpeech, LGPLv3)
// Ohne Qt TextToSpeech (VP_HAVE_TTS nicht gesetzt) bleibt alles stumm.
// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QString>

class QTextToSpeech;

class Speech : public QObject
{
    Q_OBJECT
public:
    explicit Speech(QObject *parent = nullptr);
    ~Speech() override;
    bool available() const;
    // Stimmwahl: "female" / "male" (automatisch passende Systemstimme) oder
    // "voice:<Sprache>|<Name>" für eine bestimmte installierte Stimme
    void setChoice(const QString &choice);
    void setGender(const QString &gender) { setChoice(gender); }
    QString hint() const;                    // Hinweis, wenn keine passende Stimme installiert ist
    // Auswahlliste für Einstellungen und Assistent: (Schlüssel, Anzeigetext)
    static QList<QPair<QString, QString>> voiceChoices();
    // Geschlecht einer Stimme – Engines melden es teils falsch (flite: alle "männlich")
    static int guessGender(const QString &voiceName, int engineGender);   // QVoice::Gender
    void setRate(double rate);               // -1 … 1
    QString voiceDescription() const;        // für die Anzeige
    bool isSpeaking() const;
    bool languageMatches() const;           // Stimme passt zur Oberflächensprache

    // Fehlersuche: Engine, Zustand, Fehler, Sprache und Stimmen; Engine neu starten
    static QString diagnostics();
    static void restartEngine();

    // Medizinische Kürzel und Brüche für die Aussprache ausschreiben
    static QString expand(const QString &text);

public slots:
    void say(const QString &text);
    void stop();

signals:
    void finished();

private:
    void chooseVoice();
    QTextToSpeech *m_tts = nullptr;
    QString m_choice = QStringLiteral("female");
    QString m_desc, m_hint;
    QString m_pending;          // Text, der nach dem Start der Engine vorgelesen wird
    bool m_speaking = false;
    double m_rate = 0.0;
};
