// VitaParms – Tablet oder Smartphone über das USB-Kabel abgleichen (Desktop-Seite)
//
// Verbindet sich über das Android-Hilfsprogramm adb („adb forward“) mit der Tablet-App, die
// unter Einstellungen → „USB-Abgleich mit dem Computer“ wartet (siehe usbsync.h).
// Zum Testen ohne adb: Umgebungsvariable VP_USBSYNC_DIRECT=127.0.0.1:47811
// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class UsbSyncClient;

namespace Adb {
struct Device {
    QString serial;
    QString state;     // "device", "unauthorized", "offline" …
    QString model;     // z. B. "Lenovo TB125FU"
};
QString find();                                       // Pfad zu adb(.exe) oder leer
void setPath(const QString &path);                    // vom Benutzer gewählt (gespeichert)
QList<Device> devices(const QString &adb, QString *error);
int forward(const QString &adb, const QString &serial, quint16 devicePort, QString *error);   // lokaler Port
void removeForward(const QString &adb, const QString &serial, int localPort);
} // namespace Adb

class UsbSyncDialog : public QDialog
{
    Q_OBJECT
public:
    explicit UsbSyncDialog(QWidget *parent = nullptr);
    ~UsbSyncDialog() override;

    // Beim Beenden nachfragen, ob abgeglichen werden soll (nach der ersten Verbindung eingeschaltet)
    static bool askOnExit();
    static void setAskOnExit(bool on);

    // nach „Tablet → Computer“: übernommene *.vpdb (der Aufrufer ersetzt damit die Datenbank)
    QString bundleToImport() const { return m_bundle; }
    // gleich nach dem Verbinden abgleichen (Rückfrage beim Beenden)
    void setAutoMerge(bool on) { m_autoMerge = on; }

private:
    void findDevices();
    void connectDevice(bool silent);
    void autoStart();
    void disconnectDevice();
    void doMerge();
    void doSend();
    void doFetch();
    void updateState();
    void setResult(const QString &text, bool error);
    void fitHeight();

    QString m_adb;
    QString m_serial;
    int m_localPort = 0;
    QString m_bundle;
    bool m_connected = false;
    bool m_tabletLoggedIn = false;
    bool m_sameDb = false;
    bool m_autoMerge = false;
    QString m_deviceName;

    UsbSyncClient *m_client = nullptr;
    QLabel *m_adbLabel = nullptr;
    QLabel *m_adbHint = nullptr;
    QPushButton *m_adbButton = nullptr;
    QComboBox *m_devices = nullptr;
    QLabel *m_deviceHint = nullptr;
    QPushButton *m_search = nullptr;
    QLineEdit *m_code = nullptr;
    QPushButton *m_connect = nullptr;
    QLabel *m_state = nullptr;
    QPushButton *m_merge = nullptr;
    QPushButton *m_send = nullptr;
    QPushButton *m_fetch = nullptr;
    QLabel *m_result = nullptr;
    QCheckBox *m_askExit = nullptr;
};
