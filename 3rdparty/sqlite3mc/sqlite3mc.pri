# SQLite3 Multiple Ciphers 2.6.1 (SQLite 3.54.0) – MIT-Lizenz, (c) Ulrich Telle
# https://github.com/utelle/SQLite3MultipleCiphers
#
# Unveränderte Quelldateien (alle, die sqlite3mc.c auf x86, ARM/NEON (Android) und anderen Plattformen einbindet).
# Standard-Verschlüsselung: ChaCha20-Poly1305 (256 Bit, HMAC je Seite).

INCLUDEPATH += $$PWD
SOURCES     += $$PWD/sqlite3mc.c

QMAKE_CFLAGS += -I$$PWD/aegis/include -I$$PWD/argon2/include

DEFINES += \
    SQLITE_CORE=1 \
    SQLITE_THREADSAFE=1 \
    SQLITE_DQS=0 \
    SQLITE_DEFAULT_FOREIGN_KEYS=1 \
    SQLITE_SECURE_DELETE=1 \
    SQLITE_TEMP_STORE=3 \
    SQLITE_OMIT_LOAD_EXTENSION=1 \
    SQLITE_OMIT_DEPRECATED=1

# Warnungen der Fremdbibliothek nicht anzeigen
!msvc: QMAKE_CFLAGS += -w
msvc:  QMAKE_CFLAGS += /W0

# Android/iOS: pthread steckt in der C-Bibliothek
unix:!android:!ios: LIBS += -lpthread -lm
android: LIBS += -lm
