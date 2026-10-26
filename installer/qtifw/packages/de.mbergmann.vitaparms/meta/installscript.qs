// VitaParms – Verknüpfungen im Startmenü und auf dem Desktop
function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.productType !== "windows")
        return;
    component.addOperation("CreateShortcut", "@TargetDir@/VitaParms.exe", "@StartMenuDir@/VitaParms.lnk",
        "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParms.exe", "iconId=0",
        "description=Vitalwerte-Tagebuch / Vital signs diary");
    component.addOperation("CreateShortcut", "@TargetDir@/VitaParms.exe", "@StartMenuDir@/VitaParms - Demo.lnk", "--demo",
        "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParms.exe", "iconId=0",
        "description=Demo-Datenbank / Demo database");
    component.addOperation("CreateShortcut", "@TargetDir@/VitaParms-Wartung.exe",
        "@StartMenuDir@/VitaParms aktualisieren oder entfernen.lnk", "workingDirectory=@TargetDir@",
        "description=Update or uninstall VitaParms");
    component.addOperation("CreateShortcut", "@TargetDir@/VitaParms.exe", "@DesktopDir@/VitaParms.lnk",
        "workingDirectory=@TargetDir@", "iconPath=@TargetDir@/VitaParms.exe", "iconId=0");
}
