// VitaParms light – Verknüpfungen: Windows (Startmenü, Desktop), Linux (Anwendungsmenü)
function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    var os = systemInfo.productType;
    if (os === "windows") {
        component.addOperation("CreateShortcut", "@TargetDir@/VitaParmsLight.exe", "@StartMenuDir@/VitaParms light.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParmsLight.exe", "iconId=0",
            "description=Das einfache Vitalwerte-Tagebuch / The simple vital signs diary");
        component.addOperation("CreateShortcut", "@TargetDir@/VitaParmsLight.exe", "@StartMenuDir@/VitaParms light - Demo.lnk", "--demo",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParmsLight.exe", "iconId=0",
            "description=Demo-Datenbank / Demo database");
        component.addOperation("CreateShortcut", "@TargetDir@/VitaParmsLight-Wartung.exe",
            "@StartMenuDir@/VitaParms light aktualisieren oder entfernen.lnk", "workingDirectory=@TargetDir@",
            "description=Update or uninstall VitaParms light");
        component.addOperation("CreateShortcut", "@TargetDir@/VitaParmsLight.exe", "@DesktopDir@/VitaParms light.lnk",
            "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParmsLight.exe", "iconId=0");
    } else if (os !== "osx" && os !== "macos") {
        // Linux: Einträge im Anwendungsmenü (freedesktop.org); Ordner ggf. erst anlegen
        if (!installer.fileExists(installer.value("HomeDir") + "/.local/share/applications"))
            component.addOperation("Mkdir", "@HomeDir@/.local/share/applications");
        component.addOperation("CreateDesktopEntry", "@HomeDir@/.local/share/applications/vitaparms-light.desktop",
            "Type=Application\nName=VitaParms light\nGenericName=Vitalwerte-Tagebuch\nComment=Das einfache Vitalwerte-Tagebuch\n"
            + "Exec=\"@TargetDir@/VitaParmsLight.sh\"\nIcon=@TargetDir@/vitaparms-light.png\nTerminal=false\n"
            + "Categories=Office;MedicalSoftware;\nStartupWMClass=VitaParmsLight");
        component.addOperation("CreateDesktopEntry", "@HomeDir@/.local/share/applications/vitaparms-light-demo.desktop",
            "Type=Application\nName=VitaParms light – Demo\nExec=\"@TargetDir@/VitaParmsLight.sh\" --demo\n"
            + "Icon=@TargetDir@/vitaparms-light.png\nTerminal=false\nCategories=Office;MedicalSoftware;");
    }
}
