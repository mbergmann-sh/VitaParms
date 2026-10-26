# ---------------------------------------------------------------------------
#  VitaParms – Windows: DLLs bereitstellen, Installationsverzeichnis und Installer bauen
#
#  Nach jedem erfolgreichen Windows-Build (Qt Creator oder Konsole):
#    1. windeployqt kopiert die nötigen Qt-DLLs, Plugins und Übersetzungen neben
#       VitaParms.exe – das Programm startet direkt aus dem Build-Ordner.
#  Zusätzlich beim Release-Build:
#    2. installer/install_src wird neu befüllt: VitaParms.exe, alle DLLs und
#       windeployqt-Unterordner, docs/ (Handbücher PDF + CHM), LICENSE, README
#       – ohne Makefiles, Objektdateien, .qmake.stash usw.
#    3. Qt-Installer-Framework-Pakete: installer/qtifw/packages/*/data
#    4. Ist Inno Setup installiert (Fehler beim Installerbau brechen den Build nicht ab): installer/Output/VitaParms-<Version>-Setup.exe
#       Ist das Qt Installer Framework installiert: installer/Output/VitaParms-<Version>-Setup-IFW.exe
#
#  Werkzeuge werden beim qmake-Lauf gesucht; abweichende Orte über Umgebungsvariablen:
#    VP_ISCC            Pfad zu ISCC.exe            (Vorgabe: Inno Setup 6 in Programme/Programme (x86))
#    VP_BINARYCREATOR   Pfad zu binarycreator.exe   (Vorgabe: <Qt>/Tools/QtInstallerFramework/<neueste>/bin)
#  Installer nicht automatisch bauen:   qmake CONFIG+=vp_no_installer
#
#  Alle Pfade mit Schrägstrich und in Anführungszeichen – das funktioniert unverändert in
#  cmd.exe und in einer sh.exe, falls eine fremde Toolchain sie in den PATH bringt.
#  Dateien kopiert qmake selbst (qmake -install qinstall), nicht copy/xcopy/cp.
# ---------------------------------------------------------------------------

defineReplace(vpQuote) {
    return("\"$$clean_path($$1)\"")
}

VP_INST     = $$clean_path($$PWD)
VP_STAGE    = $$VP_INST/install_src
VP_IFW_MAIN = $$VP_INST/qtifw/packages/de.mbergmann.vitaparms/data
VP_IFW_DOCS = $$VP_INST/qtifw/packages/de.mbergmann.vitaparms.docs/data
VP_OUTPUT   = $$VP_INST/Output

VP_WINDEPLOYQT = $$vpQuote($$[QT_INSTALL_BINS]/windeployqt.exe) --verbose 0

# Schlank ausliefern: VitaParms ist eine reine Widgets-Anwendung (Raster-Zeichnung, kein
# OpenGL/Direct3D/Qt Quick) und spielt nur zwei kurze WAV-Signaltöne ab. Nicht benötigt werden:
#   opengl32sw.dll (Software-OpenGL), d3dcompiler_47.dll, dxcompiler.dll/dxil.dll (Direct3D 12),
#   das FFmpeg-Multimedia-Backend samt FFmpeg-DLLs (die Signaltöne laufen über das
#   Windows-Media-Backend; ohne Backend piept VitaParms ersatzweise), Qt-Übersetzungen außer Deutsch.
# Vollständige Bereitstellung wie von windeployqt vorgesehen:  qmake CONFIG+=vp_full_deploy
!vp_full_deploy {
    VP_WINDEPLOYQT += --no-opengl-sw --no-system-d3d-compiler --translations de
    versionAtLeast(QT_VERSION, 6.8.0): \
        VP_WINDEPLOYQT += --no-system-dxc-compiler --no-ffmpeg --exclude-plugins ffmpegmediaplugin
}
VP_QINSTALL    = $$vpQuote($$QMAKE_QMAKE) -install qinstall
VP_EXE         = \"$(DESTDIR_TARGET)\"
VP_NL          = $$escape_expand(\\n\\t)

# 1. Qt-DLLs neben das Programm im Build-Ordner
QMAKE_POST_LINK += $$VP_WINDEPLOYQT $$VP_EXE

CONFIG(release, debug|release) {
    # 2. Installationsverzeichnis frisch aufbauen (führendes "-": Fehler ignorieren, wenn es fehlt)
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_STAGE))
    QMAKE_POST_LINK += $$VP_NL$$VP_WINDEPLOYQT --dir $$vpQuote($$VP_STAGE) $$VP_EXE
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL -exe $$VP_EXE $$vpQuote($$VP_STAGE/VitaParms.exe)
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../docs) $$vpQuote($$VP_STAGE/docs)
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../LICENSE) $$vpQuote($$VP_STAGE/LICENSE.txt)
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../README.md) $$vpQuote($$VP_STAGE/README.md)

    # 3. Pakete für das Qt Installer Framework (Programm und Handbücher getrennt wählbar)
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_IFW_MAIN))
    QMAKE_POST_LINK += $$VP_NL-$$QMAKE_DEL_TREE $$shell_quote($$shell_path($$VP_IFW_DOCS))
    QMAKE_POST_LINK += $$VP_NL$$VP_WINDEPLOYQT --dir $$vpQuote($$VP_IFW_MAIN) $$VP_EXE
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL -exe $$VP_EXE $$vpQuote($$VP_IFW_MAIN/VitaParms.exe)
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../LICENSE) $$vpQuote($$VP_IFW_MAIN/LICENSE.txt)
    QMAKE_POST_LINK += $$VP_NL$$VP_QINSTALL $$vpQuote($$PWD/../docs) $$vpQuote($$VP_IFW_DOCS/docs)

    # 4. Installer bauen, sofern die Werkzeuge vorhanden sind
    !vp_no_installer {
        QMAKE_POST_LINK += $$VP_NL-$$QMAKE_MKDIR $$shell_quote($$shell_path($$VP_OUTPUT))
        VP_ISCC = $$(VP_ISCC)
        isEmpty(VP_ISCC) {
            VP_ISCC_CANDIDATES = \
                "C:/Program Files (x86)/Inno Setup 6/ISCC.exe" \
                "C:/Program Files/Inno Setup 6/ISCC.exe" \
                "$$(LOCALAPPDATA)/Programs/Inno Setup 6/ISCC.exe"
            for(c, VP_ISCC_CANDIDATES): isEmpty(VP_ISCC): exists($$c): VP_ISCC = $$c
        }
        !isEmpty(VP_ISCC) {
            message("VitaParms: Inno Setup gefunden – $$VP_ISCC")
            QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_ISCC) /Q $$vpQuote($$VP_INST/VitaParms.iss)
        } else {
            message("VitaParms: Inno Setup nicht gefunden – installer/VitaParms.iss bei Bedarf von Hand übersetzen")
        }

        VP_BINARYCREATOR = $$(VP_BINARYCREATOR)
        isEmpty(VP_BINARYCREATOR) {
            VP_IFW_ROOT = $$(QTIFWDIR)
            !isEmpty(VP_IFW_ROOT): exists($$VP_IFW_ROOT/bin/binarycreator.exe): VP_BINARYCREATOR = $$VP_IFW_ROOT/bin/binarycreator.exe
        }
        isEmpty(VP_BINARYCREATOR) {
            # Qt-Online-Installer: <Qt>/<Version>/<Kit> → <Qt>/Tools/QtInstallerFramework/<Version>/bin
            VP_IFW_VERSIONS = $$files($$clean_path($$[QT_INSTALL_PREFIX]/../../Tools/QtInstallerFramework/*))
            for(d, VP_IFW_VERSIONS): exists($$d/bin/binarycreator.exe): VP_BINARYCREATOR = $$d/bin/binarycreator.exe
        }
        !isEmpty(VP_BINARYCREATOR) {
            message("VitaParms: Qt Installer Framework gefunden – $$VP_BINARYCREATOR")
            QMAKE_POST_LINK += $$VP_NL-$$vpQuote($$VP_BINARYCREATOR) --offline-only --compression 9 \
                -c $$vpQuote($$VP_INST/qtifw/config/config.xml) -p $$vpQuote($$VP_INST/qtifw/packages) \
                $$vpQuote($$VP_OUTPUT/VitaParms-$${VERSION}-Setup-IFW.exe)
        } else {
            message("VitaParms: Qt Installer Framework nicht gefunden – kein IFW-Installer")
        }
    }
}
