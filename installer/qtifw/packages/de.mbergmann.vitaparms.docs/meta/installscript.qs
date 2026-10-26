// VitaParms – Handbücher und „Erste Schritte“ im Startmenü
function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();
    if (systemInfo.productType !== "windows")
        return;
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-Handbuch.chm",
        "@StartMenuDir@/VitaParms-Handbuch (Deutsch).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-Manual.chm",
        "@StartMenuDir@/VitaParms User Manual (English).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-ErsteSchritte.chm",
        "@StartMenuDir@/VitaParms – Erste Schritte (Deutsch).lnk", "workingDirectory=@TargetDir@/docs");
    component.addOperation("CreateShortcut", "@TargetDir@/docs/VitaParms-GettingStarted.chm",
        "@StartMenuDir@/VitaParms – Getting Started (English).lnk", "workingDirectory=@TargetDir@/docs");
}
