#include <QCoreApplication>
#include <QSslSocket>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if(!QSslSocket::supportsSsl()) {
        std::cerr << "OpenSSL runtime was not loaded\n";
        return 1;
    }
    std::cout << QSslSocket::sslLibraryVersionString().toStdString() << '\n';
    // Network handshake is optional; no API key or model request is sent.
    if(app.arguments().contains("--handshake")) {
        QSslSocket socket;
        socket.connectToHostEncrypted("api.deepseek.com", 443);
        if(!socket.waitForEncrypted(15000)) {
            std::cerr << socket.errorString().toStdString() << '\n'; return 2;
        }
        std::cout << "DeepSeek TLS handshake passed\n";
    }
    return 0;
}
