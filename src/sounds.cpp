// SPDX-License-Identifier: MIT
#include "sounds.h"
#include "qtcompat.h"

#include <QApplication>
#include <QUrl>

#ifdef VP_HAVE_MULTIMEDIA
#include <QSoundEffect>
#endif

namespace Sounds {

bool available()
{
#ifdef VP_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

void preload(Kind k)
{
#ifdef VP_HAVE_MULTIMEDIA
    play(k, true);
#else
    Q_UNUSED(k)
#endif
}

void play(Kind k)
{
    play(k, false);
}

void play(Kind k, bool loadOnly)
{
#ifdef VP_HAVE_MULTIMEDIA
    static QSoundEffect *effects[4] = {};
    static const char *const files[4] = { "reminder.wav", "done.wav", "count_start.wav", "count_stop.wav" };
    const int i = int(k);
    QSoundEffect *&fx = effects[i];
    if (!fx) {
        fx = new QSoundEffect(qApp);
        fx->setSource(QUrl(QStringLiteral("qrc:/sounds/") + QLatin1String(files[i])));
        fx->setVolume(0.9f);
    }
    if (loadOnly) return;
    if (fx->status() == QSoundEffect::Error) { QApplication::beep(); return; }
    if (fx->status() != QSoundEffect::Ready) {          // erster Aufruf: nach dem Laden abspielen
        vpConnectOnce(fx, &QSoundEffect::statusChanged, fx, [fx] {
            if (fx->status() == QSoundEffect::Ready) fx->play();
            else if (fx->status() == QSoundEffect::Error) QApplication::beep();
        });
        return;
    }
    fx->play();
#else
    Q_UNUSED(k)
    if (!loadOnly) QApplication::beep();
#endif
}

} // namespace Sounds
