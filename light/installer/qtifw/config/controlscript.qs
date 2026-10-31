// VitaParms light – Steuerskript für den Qt-Installer-Framework-Installer (Windows, Linux, macOS)
//
// * Zielordner: Windows C:\VPlight, macOS /Applications/VPlight, Linux ~/VPlight (änderbar)
// * Programm nach der Installation starten: je System die passende Datei
// * Aktualisieren mit einem neuen Offline-Installer: Ist VitaParms light schon installiert, wird die
//   alte Version vor der Installation über ihr Wartungsprogramm entfernt ("purge") und die neue in
//   denselben Ordner installiert. Datenbanken und Einstellungen liegen im Benutzerprofil und
//   bleiben dabei erhalten.

function Controller()
{
    var os = systemInfo.productType;
    if (installer.isInstaller()) {
        if (os === "windows")
            installer.setValue("TargetDir", "C:/VPlight");
        else if (os === "osx" || os === "macos")
            installer.setValue("TargetDir", "/Applications/VPlight");
    }
    if (os === "windows")
        installer.setValue("RunProgram", "@TargetDir@/VitaParmsLight.exe");
    else if (os === "osx" || os === "macos")
        installer.setValue("RunProgram", "@TargetDir@/VitaParmsLight.app/Contents/MacOS/VitaParmsLight");
    else
        installer.setValue("RunProgram", "@TargetDir@/VitaParmsLight.sh");
}

function maintenanceTool()
{
    var os = systemInfo.productType;
    if (os === "windows") return "VitaParmsLight-Wartung.exe";
    if (os === "osx" || os === "macos") return "VitaParmsLight-Wartung.app/Contents/MacOS/VitaParmsLight-Wartung";
    return "VitaParmsLight-Wartung";
}

// Vorhandene Installation im (Vorgabe-)Zielordner suchen
function previousLocation()
{
    var dir = installer.value("TargetDir");
    if (installer.fileExists(dir + "/" + maintenanceTool()))
        return dir;
    return "";
}

Controller.prototype.IntroductionPageCallback = function()
{
    if (!installer.isInstaller())
        return;
    var dir = previousLocation();
    if (dir === "")
        return;
    var answer = QMessageBox.question("vitaparmslight.update",
        "VitaParms light",
        "VitaParms light ist bereits in " + dir + " installiert.\n"
        + "Die vorhandene Version wird jetzt entfernt und anschließend die neue installiert. "
        + "Ihre Daten und Einstellungen bleiben erhalten.\n\n"
        + "VitaParms light is already installed in " + dir + ".\n"
        + "The existing version will now be removed and the new one installed afterwards. "
        + "Your data and settings are kept.",
        QMessageBox.Ok | QMessageBox.Cancel);
    if (answer !== QMessageBox.Ok) {
        installer.setCanceled();
        return;
    }
    installer.execute(dir + "/" + maintenanceTool(), ["purge", "-c"]);
    installer.setValue("TargetDir", dir);
}
