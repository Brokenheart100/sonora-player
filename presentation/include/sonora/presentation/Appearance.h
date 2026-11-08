#pragma once

#include <QObject>
#include <QString>

class QQmlEngine;
class QTranslator;

namespace sonora::presentation
{

    // 外观设置：主题三态（跟随系统 / 浅色 / 深色）+ 界面语言。
    //
    // 值落在 <数据目录>/settings.ini（路径由组合根决定，这里只负责读写与生效）：
    //   ui/theme     = 0 / 1 / 2
    //   ui/language  = zh_CN / en
    //
    // 为什么放在 Presentation 而不是 Preferences 模块：它只影响视图，
    // 不参与任何业务规则；而且"切语言要立刻生效"必须能碰到 QML 引擎，
    // 组合根是唯一同时握着引擎和设置文件路径的地方。
    class Appearance final : public QObject
    {
        Q_OBJECT
        // 0 = 跟随系统，1 = 浅色，2 = 深色
        Q_PROPERTY(int themeMode READ themeMode WRITE setThemeMode NOTIFY themeModeChanged)
        // 解析后的结果（跟随系统时等于 systemDark）。QML 只读这个。
        Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
        Q_PROPERTY(bool systemDark READ systemDark NOTIFY systemDarkChanged)
        Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

    public:
        explicit Appearance(QString settingsPath, QObject *parent = nullptr);
        ~Appearance() override;

        // 引擎建好后由组合根塞进来：切语言时要让它重跑所有 qsTr 绑定。
        void setEngine(QQmlEngine *engine) noexcept { engine_ = engine; }

        [[nodiscard]] int themeMode() const noexcept { return themeMode_; }
        void setThemeMode(int mode);

        [[nodiscard]] bool dark() const noexcept;
        [[nodiscard]] bool systemDark() const noexcept { return systemDark_; }

        [[nodiscard]] QString language() const { return language_; }
        void setLanguage(const QString &language);

    signals:
        void themeModeChanged();
        void darkChanged();
        void systemDarkChanged();
        void languageChanged();

    private:
        void applyTranslator();
        void persist() const;
        void refreshSystemDark();

        QString settingsPath_;
        int themeMode_ = 0;
        QString language_ = QStringLiteral("zh_CN");
        QQmlEngine *engine_ = nullptr;
        QTranslator *translator_ = nullptr;
        bool systemDark_ = false;
    };

}
