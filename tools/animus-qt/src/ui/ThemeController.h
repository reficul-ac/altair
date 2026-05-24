#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>

class QSettings;

namespace animus
{

class ThemeController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY modeChanged)
    Q_PROPERTY(QColor window READ window NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor overlay READ overlay NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor mutedText READ mutedText NOTIFY themeChanged)
    Q_PROPERTY(QColor border READ border NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor success READ success NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    Q_PROPERTY(QColor mapBackground READ mapBackground NOTIFY themeChanged)
    Q_PROPERTY(QColor mapGrid READ mapGrid NOTIFY themeChanged)
    Q_PROPERTY(QColor mapLand READ mapLand NOTIFY themeChanged)
    Q_PROPERTY(QColor mapLandAlt READ mapLandAlt NOTIFY themeChanged)
    Q_PROPERTY(QColor sceneSkyTop READ sceneSkyTop NOTIFY themeChanged)
    Q_PROPERTY(QColor sceneSkyBottom READ sceneSkyBottom NOTIFY themeChanged)
    Q_PROPERTY(QColor sceneRidge READ sceneRidge NOTIFY themeChanged)
    Q_PROPERTY(QColor sceneRidgeDark READ sceneRidgeDark NOTIFY themeChanged)
    Q_PROPERTY(QColor sceneGroundLine READ sceneGroundLine NOTIFY themeChanged)
    Q_PROPERTY(QColor tacticalBackground READ tacticalBackground NOTIFY themeChanged)

  public:
    explicit ThemeController(QSettings *settings, QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    QString displayName() const;
    bool dark() const { return m_mode == QStringLiteral("dark"); }

    QColor window() const;
    QColor surface() const;
    QColor overlay() const;
    QColor text() const;
    QColor mutedText() const;
    QColor border() const;
    QColor accent() const;
    QColor success() const;
    QColor warning() const;
    QColor danger() const;
    QColor mapBackground() const;
    QColor mapGrid() const;
    QColor mapLand() const;
    QColor mapLandAlt() const;
    QColor sceneSkyTop() const;
    QColor sceneSkyBottom() const;
    QColor sceneRidge() const;
    QColor sceneRidgeDark() const;
    QColor sceneGroundLine() const;
    QColor tacticalBackground() const;

  public slots:
    void setMode(const QString &mode);
    void toggleMode();

  public:
    void setModeOverride(const QString &mode);

  signals:
    void modeChanged();
    void themeChanged();

  private:
    static QString normalizeMode(const QString &mode);
    QColor lightDark(const QColor &light, const QColor &dark) const;
    void applyMode(const QString &mode, bool persist);

    QPointer<QSettings> m_settings;
    QString m_mode = QStringLiteral("light");
};

} // namespace animus
