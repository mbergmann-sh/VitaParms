// VitaParms – Steuerskript für den Qt-Installer-Framework-Installer
//
// Aktualisieren mit einem neuen Offline-Installer: Ist VitaParms schon installiert, wird die
// alte Version vor der Installation über ihr Wartungsprogramm entfernt ("purge") und die neue
// in denselben Ordner installiert. Datenbanken und Einstellungen liegen im Benutzerprofil und
// bleiben dabei erhalten.

var kTool = "VitaParms-Wartung.exe";

function Controller()
{
}

// Vorhandene Installation im (Vorgabe-)Zielordner suchen. Den Uninstall-Schlüssel in der
// Registrierung kann das Skript nicht nutzen: IFW benennt ihn nach einer zufälligen UUID.
function previousLocation()
{
    var dir = installer.value("TargetDir");
    if (installer.fileExists(dir + "/" + kTool))
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
    var answer = QMessageBox.question("vitaparms.update",
        "VitaParms",
        "VitaParms ist bereits in " + dir + " installiert.\n"
        + "Die vorhandene Version wird jetzt entfernt und anschließend die neue installiert. "
        + "Ihre Datenbanken und Einstellungen bleiben erhalten.\n\n"
        + "VitaParms is already installed in " + dir + ".\n"
        + "The existing version will now be removed and the new one installed afterwards. "
        + "Your databases and settings are kept.",
        QMessageBox.Ok | QMessageBox.Cancel);
    if (answer !== QMessageBox.Ok) {
        installer.setCanceled();
        return;
    }
    installer.execute(dir + "/" + kTool, ["purge", "-c"]);
    installer.setValue("TargetDir", dir);
}
