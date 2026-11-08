#include "sonora/presentation/Appearance.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlEngine>
#include <QSettings>
#include <QStyleHints>
#include <QTranslator>

#include <algorithm>

namespace sonora::presentation
{

    namespace
    {
        constexpr auto kThemeKey = "ui/theme";
        constexpr auto kLanguageKey = "ui/language";

        // zh_CN 是源语言，不需要 .qm；只认这两条。
        QString normalizeLanguage(const QString &value)
        {
            return value.startsWith(QStringLiteral("en"), Qt::CaseInsensitive) ? QStringLiteral("en")
                                                                              : QStringLiteral("zh_CN");
        }
    }

    Appearance::Appearance(QString settingsPath, QObject *parent)
        : QObject(parent), settingsPath_(std::move(settingsPath))
    {
        {
            const QSettings settings{settingsPath_, QSettings::IniFormat};
            themeMode_ = std::clamp(settings.value(QString::fromLatin1(kThemeKey), 0).toInt(), 0, 2);
            language_ = normalizeLanguage(settings.value(QString::fromLatin1(kLanguageKey),
                                                         QStringLiteral("zh_CN"))
                                              .toString());
        }

        refreshSystemDark();
        if (auto *hints = QGuiApplication::styleHints())
        {
            connect(hints, &QStyleHints::colorSchemeChanged, this,
                    [this](Qt::ColorScheme)
                    {
                        const bool before = dark();
                        refreshSystemDark();
                        emit systemDarkChanged();
                        if (dark() != before)
                        {
                            emit darkChanged();
                        }
                    });
        }

        // 源语言是中文，什么都不装就是中文；英文要装 .qm。
        applyTranslator();
        QLocale::setDefault(QLocale(language_));
    }

    Appearance::~Appearance()
    {
        if (translator_ != nullptr)
        {
            QCoreApplication::removeTranslator(translator_);
            delete translator_;
            translator_ = nullptr;
        }
    }

    void Appearance::refreshSystemDark()
    {
        const auto *hints = QGuiApplication::styleHints();
        systemDark_ = hints != nullptr && hints->colorScheme() == Qt::ColorScheme::Dark;
    }

    bool Appearance::dark() const noexcept
    {
        return themeMode_ == 2 || (themeMode_ == 0 && systemDark_);
    }

    void Appearance::setThemeMode(int mode)
    {
        const int clamped = std::clamp(mode, 0, 2);
        if (clamped == themeMode_)
        {
            return;
        }

        const bool before = dark();
        themeMode_ = clamped;
        persist();
        emit themeModeChanged();
        if (dark() != before)
        {
            emit darkChanged();
        }
    }

    void Appearance::setLanguage(const QString &language)
    {
        const QString normalized = normalizeLanguage(language);
        if (normalized == language_)
        {
            return;
        }

        language_ = normalized;
        applyTranslator();
        QLocale::setDefault(QLocale(language_));
        persist();
        emit languageChanged();
    }

    void Appearance::applyTranslator()
    {
        if (translator_ != nullptr)
        {
            QCoreApplication::removeTranslator(translator_);
            delete translator_;
            translator_ = nullptr;
        }

        if (language_ == QStringLiteral("en"))
        {
            auto *translator = new QTranslator{this};
            if (translator->load(QStringLiteral(":/i18n/sonora_en.qm")))
            {
                QCoreApplication::installTranslator(translator);
                translator_ = translator;
            }
            else
            {
                // 没编进去就退回中文：宁可整屏中文，也不要半中半英还找不到原因。
                delete translator;
                std::fprintf(stderr, "[i18n] 找不到 :/i18n/sonora_en.qm，界面保持中文\n");
            }
        }

        // 已经显示出来的界面不必重启：Qt 6.5+ 会把载入过的 qsTr 绑定重跑一遍。
        if (engine_ != nullptr)
        {
            engine_->retranslate();
        }
    }

    void Appearance::persist() const
    {
        QSettings settings{settingsPath_, QSettings::IniFormat};
        settings.setValue(QString::fromLatin1(kThemeKey), themeMode_);
        settings.setValue(QString::fromLatin1(kLanguageKey), language_);
    }

}
