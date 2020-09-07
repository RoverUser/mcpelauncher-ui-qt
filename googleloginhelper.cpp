#include "googleloginhelper.h"

#include <googleloginwindow.h>
#include <QStandardPaths>
#include <QDir>
#include <QWindow>
#include <QtConcurrent>
#include "supportedandroidabis.h"

std::string GoogleLoginHelper::getTokenCachePath() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("playapi_token_cache.conf").toStdString();
}

GoogleLoginHelper::GoogleLoginHelper() : loginCache(getTokenCachePath()), login(device, loginCache) {
    settings.beginGroup("googlelogin");
    if (settings.contains("identifier")) {
        currentAccount.setAccountIdentifier(settings.value("identifier").toString());
        currentAccount.setAccountUserId(settings.value("userId").toString());
        currentAccount.setAccountToken(settings.value("token").toString());
        login.set_token(currentAccount.accountIdentifier().toStdString(), currentAccount.accountToken().toStdString());
        settings.endGroup();
        loadDeviceState();
        hasAccount = true;
    } else {
        settings.endGroup();
    }
    
}

GoogleLoginHelper::~GoogleLoginHelper() {
    delete window;
}

void GoogleLoginHelper::loadDeviceState() {
    settings.beginGroup("device_state");
    device.generated_mac_addr = settings.value("generated_mac_addr").toString().toStdString();
    device.generated_meid = settings.value("generated_meid").toString().toStdString();
    device.generated_serial_number = settings.value("generated_serial_number").toString().toStdString();
    device.random_logging_id = settings.value("generated_serial_number").toLongLong();
    int size = settings.beginReadArray("native_platforms");
    if(size) {
        device.config_native_platforms.clear();
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            device.config_native_platforms.emplace_back(settings.value("platform").toString().toStdString());
        }
    } else {
        device.config_native_platforms = { "x86" };
    }
    settings.endArray();
    settings.endGroup();
}

void GoogleLoginHelper::saveDeviceState() {
    settings.beginGroup("device_state");
    settings.setValue("generated_mac_addr", QString::fromStdString(device.generated_mac_addr));
    settings.setValue("generated_meid", QString::fromStdString(device.generated_meid));
    settings.setValue("generated_serial_number", QString::fromStdString(device.generated_serial_number));
    settings.setValue("random_logging_id", device.random_logging_id);
    settings.beginWriteArray("native_platforms", device.config_native_platforms.size());
    for (int i = 0; i < device.config_native_platforms.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("platform", QString::fromStdString(device.config_native_platforms[i]));
    }
    settings.endArray();
    settings.endGroup();
}

void GoogleLoginHelper::acquireAccount(QWindow *parent) {
    if (window)
        return;
    window = new GoogleLoginWindow();
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->winId();
    window->windowHandle()->setTransientParent(parent);
    window->move(parent->x() + parent->width() / 2 - window->width() / 2, parent->y() + parent->height() / 2 - window->height() / 2);
    window->show();
    connect(window, &QDialog::finished, this, &GoogleLoginHelper::onLoginFinished);
}

void GoogleLoginHelper::onLoginFinished(int code) {
    if (code == QDialog::Accepted) {
        try {
            device.config_native_platforms = {};
            for (auto&& e : SupportedAndroidAbis::getAbis()) {
                if(e.second.empty()) {
                    device.config_native_platforms.push_back(e.first);
                }
            }
            login.perform_with_access_token(window->accountToken().toStdString(), window->accountIdentifier().toStdString(), true)->call();
            currentAccount.setAccountIdentifier(window->accountIdentifier());
            currentAccount.setAccountUserId(window->accountUserId());
            currentAccount.setAccountToken(QString::fromStdString(login.get_token()));
            settings.beginGroup("googlelogin");
            settings.setValue("identifier", currentAccount.accountIdentifier());
            settings.setValue("userId", currentAccount.accountUserId());
            settings.setValue("token", currentAccount.accountToken());
            settings.endGroup();
            saveDeviceState();
            hasAccount = true;
            accountAcquireFinished(&currentAccount);
        } catch (const std::exception& ex) {
            loginError(ex.what());
            accountAcquireFinished(nullptr);
        }
    } else {
        accountAcquireFinished(nullptr);
    }
    emit accountInfoChanged();
    window = nullptr;
}

void GoogleLoginHelper::signOut() {
    hasAccount = false;
    currentAccount.setAccountIdentifier("");
    currentAccount.setAccountUserId("");
    currentAccount.setAccountToken("");
    settings.remove("googlelogin");
    settings.remove("checkin");
    settings.remove("device_state");
    settings.remove("playapi");
    loginCache.clear();
    emit accountInfoChanged();
}

QStringList GoogleLoginHelper::getDeviceStateABIs(bool showUnsupported) {
    if (hasAccount) {
        auto supportedabis = SupportedAndroidAbis::getAbis();
        QStringList abis;
        for (auto&& abi : device.config_native_platforms) {
            if (!showUnsupported) {
                auto res = supportedabis.find(abi);
                if(res == supportedabis.end() || !res->second.empty()) {
                    continue;
                }
            }
            abis.append(QString::fromStdString(abi));
        }
        return abis;
    } else {
        return {};
    }
}

QStringList GoogleLoginHelper::getAbis() {
    QStringList abis;
    for (auto&& abi : SupportedAndroidAbis::getAbis()) {
        abis.append(QString::fromStdString(abi.first));
    }
    return abis;
}

QString GoogleLoginHelper::GetSupportReport() {
    QString report;
    for (auto&& abi : SupportedAndroidAbis::getAbis()) {
        report.append("<b>" + QString::fromStdString(abi.first) + "</b> is " + (abi.second.empty() ? "<b><font color=\"#00cc00\">Supported</font></b><br/>" : "<b><font color=\"#FF0000\">Unsupported</font></b>:<br/>" + QString::fromStdString(abi.second) + "<br/>"));
    }
    return report;
}

bool GoogleLoginHelper::hideLatest() {
    if (!hasAccount || device.config_native_platforms.empty()) {
        return true;
    }
    auto supportedabis = SupportedAndroidAbis::getAbis();
    auto res = supportedabis.find(device.config_native_platforms[0]);
    return res == supportedabis.end() || !res->second.empty();
}

bool GoogleLoginHelper::isSupported() {
    for (auto &&abi : SupportedAndroidAbis::getAbis()) {
        if (abi.second.empty()) {
            return true;
        }
    }
    return false;
}