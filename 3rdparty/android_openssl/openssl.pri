# VitaParms – mitgelieferte OpenSSL-3-Bibliotheken für Android (HTTPS/TLS)
# Quelle: https://github.com/KDAB/android_openssl (Stand b71f147, OpenSSL 3.1.8), Apache-2.0-Lizenz
# (LICENSE.txt). Qt lädt libssl_3.so/libcrypto_3.so zur Laufzeit; androiddeployqt nimmt je ABI die
# passenden Dateien ins Paket.
!contains(QT.network_private.enabled_features, openssl-linked) {
    for (abi, $$list(arm64-v8a armeabi-v7a x86_64)) {
        ANDROID_EXTRA_LIBS += $$PWD/ssl_3/$$abi/libcrypto_3.so $$PWD/ssl_3/$$abi/libssl_3.so
    }
}
