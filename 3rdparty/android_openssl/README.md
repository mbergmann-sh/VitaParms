# OpenSSL 3 für die Android-App

Vorgefertigte OpenSSL-3.1.8-Bibliotheken (`libssl_3.so`, `libcrypto_3.so`) für die ABIs
arm64-v8a (Tablets/Smartphones), armeabi-v7a (ältere Geräte) und x86_64 (Emulator).
Qt braucht sie auf Android für HTTPS – also für den Abgleich über die eigene Website.

* Quelle: https://github.com/KDAB/android_openssl, Commit b71f1470962019bd89534a2919f5925f93bc5779
  (26.01.2026), Ordner `ssl_3/`
* Lizenz: Apache License 2.0 (`LICENSE.txt`)
* Eingebunden von `mobile/VitaParmsMobile.pro` über `openssl.pri`. Eine andere Version lässt sich
  mit der Umgebungsvariablen `VP_ANDROID_OPENSSL` (Ordner mit einer `openssl.pri`) verwenden.
