// VitaParms light – Benutzerhandbuch und „Erste Schritte“ im Startmenü (Windows)
function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.productType !== "windows")
        return;
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-light-Benutzerhandbuch.chm",
        "@StartMenuDir@/Benutzerhandbuch (Deutsch).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-light-UserGuide.chm",
        "@StartMenuDir@/User Guide (English).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-light-ErsteSchritte.chm",
        "@StartMenuDir@/Erste Schritte (Deutsch).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-light-GettingStarted.chm",
        "@StartMenuDir@/Getting Started (English).lnk", "workingDirectory=@TargetDir@/docs");
}
