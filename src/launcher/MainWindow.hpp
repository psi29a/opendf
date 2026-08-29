#pragma once

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressDialog;
class QPushButton;
QT_END_NAMESPACE

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void browsePath();
    void installFromArchive();
    void showGetGameDialog();
    void validatePath();
    void play();

private:
    void loadSettings();
    void saveSettings() const;

    // Writes settings.cfg (data-root + the shipped data dirs) and opendf.cfg
    // ([CVars] resolution) into the engine's config dir. The engine takes no
    // data-root argument, so this is the only way to point it at the game.
    bool writeEngineConfig(QString *err) const;

    QString engineBinaryPath() const;

    // Unpacks an ARENA2 tree out of a DaggerfallSetup .exe/.zip (via
    // innoextract) or a plain .zip, into destRoot. Returns the directory
    // holding the game files, or "" with *err set.
    QString installGameData(const QString &archivePath, const QString &destRoot,
                            QString *err, QProgressDialog *progress);

    QLineEdit   *mPathEdit   {};
    QLabel      *mPathStatus {};
    QComboBox   *mResolution {};
    QCheckBox   *mFullscreen {};
    QCheckBox   *mDevparm    {};
    QPushButton *mPlayBtn    {};
};
