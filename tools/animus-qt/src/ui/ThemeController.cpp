#include "ui/ThemeController.h"

#include <QSettings>

namespace animus
{
namespace
{
QString themeModeSetting()
{
    return QStringLiteral("ui/themeMode");
}
}

ThemeController::ThemeController(QSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings)
{
    if (m_settings)
        m_mode = normalizeMode(m_settings->value(themeModeSetting()).toString());
}

QString ThemeController::displayName() const
{
    return dark() ? QStringLiteral("Dark") : QStringLiteral("Light");
}

void ThemeController::setMode(const QString &mode)
{
    applyMode(mode, true);
}

void ThemeController::setModeOverride(const QString &mode)
{
    applyMode(mode, false);
}

void ThemeController::applyMode(const QString &mode, bool persist)
{
    const QString normalized = normalizeMode(mode);
    if (normalized == m_mode)
        return;
    m_mode = normalized;
    if (persist && m_settings)
        m_settings->setValue(themeModeSetting(), m_mode);
    emit modeChanged();
    emit themeChanged();
}

void ThemeController::toggleMode()
{
    setMode(dark() ? QStringLiteral("light") : QStringLiteral("dark"));
}

QString ThemeController::normalizeMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized == QStringLiteral("dark"))
        return QStringLiteral("dark");
    return QStringLiteral("light");
}

QColor ThemeController::lightDark(const QColor &light, const QColor &darkColor) const
{
    return dark() ? darkColor : light;
}

QColor ThemeController::window() const
{
    return lightDark(QColor(QStringLiteral("#eef3ee")), QColor(QStringLiteral("#111820")));
}

QColor ThemeController::surface() const
{
    return lightDark(QColor(QStringLiteral("#f7f7f3")), QColor(QStringLiteral("#1b2530")));
}

QColor ThemeController::overlay() const
{
    return lightDark(QColor(247, 247, 243, 240), QColor(27, 37, 48, 235));
}

QColor ThemeController::text() const
{
    return lightDark(QColor(QStringLiteral("#202020")), QColor(QStringLiteral("#eef4f2")));
}

QColor ThemeController::mutedText() const
{
    return lightDark(QColor(QStringLiteral("#4b5563")), QColor(QStringLiteral("#a8b4bd")));
}

QColor ThemeController::border() const
{
    return lightDark(QColor(QStringLiteral("#c9c9c0")), QColor(QStringLiteral("#40505d")));
}

QColor ThemeController::accent() const
{
    return lightDark(QColor(QStringLiteral("#1d6fd6")), QColor(QStringLiteral("#65a7ff")));
}

QColor ThemeController::success() const
{
    return lightDark(QColor(QStringLiteral("#0f7b43")), QColor(QStringLiteral("#49c979")));
}

QColor ThemeController::warning() const
{
    return lightDark(QColor(QStringLiteral("#7a4b00")), QColor(QStringLiteral("#f3b54b")));
}

QColor ThemeController::danger() const
{
    return lightDark(QColor(QStringLiteral("#b32020")), QColor(QStringLiteral("#ff6b63")));
}

QColor ThemeController::mapBackground() const
{
    return lightDark(QColor(QStringLiteral("#dfe7de")), QColor(QStringLiteral("#24313a")));
}

QColor ThemeController::mapGrid() const
{
    return lightDark(QColor(QStringLiteral("#c6d0c5")), QColor(QStringLiteral("#33434c")));
}

QColor ThemeController::mapLand() const
{
    return lightDark(QColor(QStringLiteral("#b9c7bd")), QColor(QStringLiteral("#41534d")));
}

QColor ThemeController::mapLandAlt() const
{
    return lightDark(QColor(QStringLiteral("#ccd7cb")), QColor(QStringLiteral("#52655b")));
}

QColor ThemeController::sceneSkyTop() const
{
    return lightDark(QColor(QStringLiteral("#8fb1ce")), QColor(QStringLiteral("#172230")));
}

QColor ThemeController::sceneSkyBottom() const
{
    return lightDark(QColor(QStringLiteral("#e6ece8")), QColor(QStringLiteral("#2f3d3c")));
}

QColor ThemeController::sceneRidge() const
{
    return lightDark(QColor(QStringLiteral("#879a83")), QColor(QStringLiteral("#4e665d")));
}

QColor ThemeController::sceneRidgeDark() const
{
    return lightDark(QColor(QStringLiteral("#5f775f")), QColor(QStringLiteral("#2f4d44")));
}

QColor ThemeController::sceneGroundLine() const
{
    return lightDark(QColor(QStringLiteral("#d7e6d0")), QColor(QStringLiteral("#8db09a")));
}

QColor ThemeController::tacticalBackground() const
{
    return lightDark(QColor(QStringLiteral("#e9eef0")), QColor(QStringLiteral("#101820")));
}

} // namespace animus
