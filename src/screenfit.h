// VitaParms – Fenster an kleine Bildschirme anpassen (z. B. 1366 × 768)
//
// Für alle Fenster des Programms (app-weiter Ereignisfilter, beim Anzeigen):
//  * Ist der Inhalt eines Dialogs höher oder breiter als der freie Bildschirmbereich, kommt er in eine
//    rollbare Fläche (QScrollArea); die Schaltflächenleiste (OK/Abbrechen …) bleibt außerhalb und damit
//    immer sichtbar.
//  * Jedes Fenster wird auf den freien Bildschirmbereich begrenzt und so verschoben, dass es ganz
//    sichtbar ist (auch wenn eine gespeicherte Größe von einem größeren Bildschirm stammt).
// Dialoge von Qt selbst (Datei-, Druck-, Meldungsdialoge) bleiben unberührt.
#pragma once

class QWidget;
class QApplication;

namespace ScreenFit {

// Ereignisfilter für die ganze Anwendung einrichten (einmal in main()).
void install(QApplication &app);

// Ein einzelnes Fenster sofort anpassen (macht der Filter beim Anzeigen automatisch).
void fit(QWidget *window);

}
