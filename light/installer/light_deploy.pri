# ---------------------------------------------------------------------------
#  VitaParms light – Bereitstellung und Installer (Qt Installer Framework) für Windows, Linux, macOS
#
#  Release-Build (Qt Creator oder Konsole):
#    1. Programm samt Qt-Bibliotheken in das IFW-Paket kopieren
#         Windows: windeployqt            → qtifw/packages/de.mbergmann.vitaparms.light/data
#         Linux:   linux_deploy.sh (ldd) → …/data (lib/, plugins/, qt.conf, Startskript VitaParmsLight.sh)
#         macOS:   macdeployqt            → …/data/VitaParmsLight.app
#    2. Handbücher (PDF, unter Windows auch CHM) in das Paket …light.docs/data/docs
#    3. Windows: nach JEDEM Build windeployqt neben das Programm; beim Release-Build zusätzlich
#       das Installationsverzeichnis light/installer/install_src (für Inno Setup)
#    4. Installer in light/installer/Output, soweit die Werkzeuge vorhanden sind
#         Windows: VitaParms-light-<Version>-Setup.exe (Inno Setup, VP_ISCC oder Standardort)
#                  VitaParms-light-<Version>-Setup-IFW.exe (Qt Installer Framework)
#         Linux:   VitaParms-light-<Version>-Setup.run
#         macOS:   VitaParms-light-<Version>-Setup.dmg (Installer-App im Disk-Image)
#
#  binarycreator wird gesucht über VP_BINARYCREATOR (Pfad zur Datei), QTIFWDIR bzw.
#  <Qt>/Tools/QtInstallerFramework/<Version>/bin (Qt-Online-Installer). Fehlt es, entstehen nur die
#  Paketdaten; den Installer dann von Hand erzeugen (siehe light/installer/README.md).
#  Abschalten: qmake CONFIG+=vp_no_installer. Fehler beim Installerbau brechen den Build nicht ab.
# ---------------------------------------------------------------------------

defineReplace(vpQuote) {
    return("\"$$clean_path($$1)\"")
}

VP_LINST    = $$clean_path($$PWD)
VP_LPKG     = $$VP_LINST/qtifw/packages/de.mbergmann.vitaparms.light/data
VP_LDOCS    = $$VP_LINST/qtifw/packages/de.mbergmann.vitaparms.light.docs/data
VP_LOUTPUT  = $$VP_LINST/Output
VP_NL       = $$escape_expand(\\n\\t)
VP_QINSTALL = $$vpQuote($$QMAKE_QMAKE) -install qinstall

# ---- binarycreator suchen
VP_BIN_EXT =
win32: VP_BIN_EXT = .exe
VP_BINARYCREATOR = $$(VP_BINARYCREATOR)
isEmpty(VP_BINARYCREATOR) {
    VP_IFW_ROOT = $$(QTIFWDIR)
    !isEmpty(VP_IFW_ROOT): exists($$VP_IFW_ROOT/bin/binarycreator$$VP_BIN_EXT): VP_BINARYCREATOR = $$VP_IFW_ROOT/bin/binarycreator$$VP_BIN_EXT
}
isEmpty(VP_BINARYCREATOR) {
    VP_IFW_VERSIONS = $$files($$clean_path($$[QT_INSTALL_PREFIX]/../../Tools/QtInstallerFramework/*))
    for(d, VP_IFW_VERSIONS): exists($$d/bin/binarycreator$$VP_BIN_EXT): VP_BINARYCREATOR = $$d/bin/binarycreator$$VP_BIN_EXT
}
vp_no_installer: VP_BINARYCREATOR =
!isEmpty(VP_BINARYCREATOR): message("VitaParms light: Qt Installer Framework gefunden – $$VP_BINARYCREATOR")
else: message("VitaParms light: Qt Installer Framework nicht gefunden – nur Paketdaten (light/installer/qtifw/packages)")

VP_IFW_ARGS = --offline-only --compression 9 -c $$vpQuote($$VP_LINST/qtifw/config/config.xml) -p $$vpQuote($$VP_LINST/qtifw/packages)

win32 {
    VP_EXE = \"$(DESTDIR_TARGET)\"
    VP_LSTAGE = $$VP_LINST/install_src
    VP_WINDEPLOYQT = $$vpQuote($$[QT_INSTALL_BINS]/windeployqt.exe) --verbose 0
    # schlank ausliefern wie VitaParms (siehe installer/windows_deploy.pri); alles: CONFIG+=vp_full_deploy
    !vp_full_deploy {
        VP_WINDEPLOYQT += --no-opengl-sw --no-system-d3d-compiler --translations de
        versionAtLeast(QT_VERSION, 6.8.0): \
            VP_WINDEPLOYQT += --no-system-dxc-compiler --no-ffmpeg --exclude-plugins ffmpegmediaplugin
    }
    # Qt-DLLs nach jedem Build (Debug und Release) neben das Programm
    QMAKE_POST_LINK += $$VP_WINDEPLOYQT $$VP_EXE

    # Inno Setup suchen (VP_ISCC oder Standardorte)
    VP_ISCC = $$(VP_ISCC)
    isEmpty(VP_ISCC) {
        VP_ISCC_CANDIDATES = \
            "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" \
            "C:/Program Files/Inno Setup 6/ISCC.exe" \
            "$$(LOCALAPPDATA)/Programs/Inno Setup 6/ISCC.exe"
        for(c, VP_ISCC_CANDIDATES): isEmpty(VP_ISCC): exists($$c): VP_ISCC = $$c
    }
    vp_no_installer: VP_ISCC =
    !isEmpty(VP_ISCC): message("VitaParms light: Inno Setup gefunden – $$VP_ISCC")
    else: message("VitaParms light: Inno Setup nicht gefunden – light/installer/VitaParmsLight.iss bei Bedarf von Hand übersetzen")
    CONFIG(debug, debug|release): message("VitaParms light: Debug-Build – Installationsverzeichnis und Installer entstehen nur beim Release-Build")
}

CONFIG(release, debug|release) {
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_LPKG))
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_LDOCS))
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_MKDIR $$shell_quote($$shell_path($$VP_LPKG))
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_MKDIR $$shell_quote($$shell_path($$VP_LDOCS/docs))
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_MKDIR $$shell_quote($$shell_path($$VP_LOUTPUT))
    for(f, VP_DOCS): QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$f) $$vpQuote($$VP_LDOCS/docs/$$basename(f))

    win32 {
        # Installationsverzeichnis für Inno Setup frisch aufbauen (wie bei VitaParms: installer/install_src)
        QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_LSTAGE))
        QMAKE_POST_LINK += $$VP_NL$$VP_WINDEPLOYQT --dir $$vpQuote($$VP_LSTAGE) $$VP_EXE
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL -exe $$VP_EXE $$vpQuote($$VP_LSTAGE/VitaParmsLight.exe)
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../docs) $$vpQuote($$VP_LSTAGE/docs)
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../../LICENSE) $$vpQuote($$VP_LSTAGE/LICENSE.txt)
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/README.md) $$vpQuote($$VP_LSTAGE/README.md)
        # Paket für das Qt Installer Framework
        QMAKE_POST_LINK += $$VP_NL$$VP_WINDEPLOYQT --dir $$vpQuote($$VP_LPKG) $$VP_EXE
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL -exe $$VP_EXE $$vpQuote($$VP_LPKG/VitaParmsLight.exe)
        QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../../LICENSE) $$vpQuote($$VP_LPKG/LICENSE.txt)
        # Installer: Inno Setup (…-Setup.exe) und Qt Installer Framework (…-Setup-IFW.exe), soweit vorhanden
        !isEmpty(VP_ISCC): QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_ISCC) /Q $$vpQuote($$PWD/VitaParmsLight.iss)
        !isEmpty(VP_BINARYCREATOR): QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_BINARYCREATOR) $$VP_IFW_ARGS \
            $$vpQuote($$VP_LOUTPUT/VitaParms-light-$${VERSION}-Setup-IFW.exe)
    }
    macx {
        VP_APP = $$OUT_PWD/$${TARGET}.app
        QMAKE_POST_LINK += $$VP_NL$$vpQuote($$[QT_INSTALL_BINS]/macdeployqt) $$vpQuote($$VP_APP) -always-overwrite
        QMAKE_POST_LINK += $$VP_NL cp -R $$vpQuote($$VP_APP) $$vpQuote($$VP_LPKG/)
        QMAKE_POST_LINK += $$VP_NL cp $$vpQuote($$PWD/../../LICENSE) $$vpQuote($$VP_LPKG/LICENSE.txt)
        !isEmpty(VP_BINARYCREATOR) {
            VP_SETUP = VitaParms-light-$${VERSION}-Setup
            QMAKE_POST_LINK += $$VP_NL-rm -rf $$vpQuote($$VP_LOUTPUT/$${VP_SETUP}.app) $$vpQuote($$VP_LOUTPUT/$${VP_SETUP}.dmg)
            QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_BINARYCREATOR) $$VP_IFW_ARGS $$vpQuote($$VP_LOUTPUT/$${VP_SETUP}.app)
            QMAKE_POST_LINK += $$VP_NL-hdiutil create -volname \"VitaParms light\" -srcfolder $$vpQuote($$VP_LOUTPUT/$${VP_SETUP}.app) \
                -ov -format UDZO $$vpQuote($$VP_LOUTPUT/$${VP_SETUP}.dmg)
        }
    }
    unix:!macx {
        QMAKE_POST_LINK += $$VP_NL sh $$vpQuote($$PWD/linux_deploy.sh) $$vpQuote($$OUT_PWD/$$TARGET) $$vpQuote($$QMAKE_QMAKE) \
            $$vpQuote($$VP_LPKG) $$vpQuote($$PWD/../resources/vitaparms-light.png)
        QMAKE_POST_LINK += $$VP_NL cp $$vpQuote($$PWD/../../LICENSE) $$vpQuote($$VP_LPKG/LICENSE.txt)
        !isEmpty(VP_BINARYCREATOR): QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_BINARYCREATOR) $$VP_IFW_ARGS \
            $$vpQuote($$VP_LOUTPUT/VitaParms-light-$${VERSION}-Setup.run)
    }
}
