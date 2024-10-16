#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE 1

#include <polkitagent/polkitagent.h>
#include <print>

#include "Agent.hpp"
#include "../QMLIntegration.hpp"
#include "../SigDaemon.hpp"

CAgent::CAgent() {
    ;
}

CAgent::~CAgent() {
    ;
}

bool CAgent::start() {
    sessionSubject = makeShared<PolkitQt1::UnixSessionSubject>(getpid());

    listener.registerListener(*sessionSubject, "/org/hyprland/PolicyKit1/AuthenticationAgent");

    int          argc = 1;
    char*        argv = (char*)"hyprpolkitagent";
    QApplication app(argc, &argv);

    sigDaemon = makeShared<CSigDaemon>();

    app.setApplicationName("Hyprland Polkit Agent");
    QGuiApplication::setQuitOnLastWindowClosed(false);

    app.exec();

    return true;
}

void CAgent::resetAuthState() {
    if (authState.authing) {
        authState.authing = false;

        if (authState.qmlEngine)
            authState.qmlEngine->deleteLater();
        if (authState.qmlIntegration)
            authState.qmlIntegration->deleteLater();

        authState.qmlEngine      = nullptr;
        authState.qmlIntegration = nullptr;
    }
}

void CAgent::initAuthPrompt() {
    resetAuthState();

    if (!listener.session.inProgress) {
        std::print(stderr, "INTERNAL ERROR: Spawning qml prompt but session isn't in progress\n");
        return;
    }

    std::print("Spawning qml prompt\n");

    authState.authing = true;

    authState.qmlIntegration = new CQMLIntegration();

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE"))
        QQuickStyle::setStyle("org.kde.desktop");

    authState.qmlEngine = new QQmlApplicationEngine();
    authState.qmlEngine->rootContext()->setContextProperty("hpa", authState.qmlIntegration);
    authState.qmlEngine->load(QUrl{u"qrc:/qt/qml/hpa/qml/main.qml"_qs});

    authState.qmlIntegration->focusField();
}

bool CAgent::resultReady() {
    std::lock_guard<std::mutex> lg(lastAuthResult.resultMutex);

    return !lastAuthResult.used;
}

void CAgent::submitResultThreadSafe(const std::string& result) {
    std::lock_guard<std::mutex> lg(lastAuthResult.resultMutex);
    lastAuthResult.used   = false;
    lastAuthResult.result = result;

    const bool PASS = result.starts_with("auth:");

    std::print("Got result from qml: {}\n", PASS ? "auth:**PASSWORD**" : result);

    if (PASS)
        listener.submitPassword(result.substr(result.find(":") + 1).c_str());
    else
        listener.cancelPending();
}

void CAgent::setAuthError(const QString& err) {
    if (!authState.qmlIntegration)
        return;

    authState.qmlIntegration->setErrorString(err);
}