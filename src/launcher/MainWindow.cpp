#include "MainWindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int WINDOW_W = 560;

// The engine opens all six of these by name at startup (see
// components/vfs/manager.cpp). ARCH3D.BSA and DAGGER.SND are the ones a raw
// CD image lacks -- they only exist after the original installer has run --
// so they're what makes a folder a *playable* data root rather than just a
// copy of the disc.
const char *const REQUIRED_FILES[] = {
    "MAPS.BSA", "BLOCKS.BSA", "MONSTER.BSA", "MIDI.BSA",
    "ARCH3D.BSA", "DAGGER.SND",
};

constexpr auto BETHESDA_URL = "https://elderscrolls.bethesda.net/en-EU/daggerfall";
constexpr auto STEAM_URL    = "https://store.steampowered.com/app/1812390/The_Elder_Scrolls_II_Daggerfall/";
constexpr auto GOG_URL      = "https://www.gog.com/en/game/the_elder_scrolls_chapter_ii_daggerfall";
constexpr auto UESP_URL     = "https://en.uesp.net/wiki/Daggerfall:Files";
constexpr auto DFSETUP_URL  = "https://theelderscrolls.wiwiland.net/Fichiers/DaggerfallSetup-3.2.0.zip";

// Daggerfall's own files are uppercase, but installs copied off a CD by other
// tools (or unpacked on a case-sensitive filesystem) are often lowercase. The
// engine itself is case-sensitive on Linux; this check is only about telling
// the user whether the folder looks right, so it accepts either spelling.
bool hasGameFile(const QString &dir, const char *name)
{
    const QDir d(dir);
    return QFileInfo::exists(d.filePath(QString::fromLatin1(name)))
        || QFileInfo::exists(d.filePath(QString::fromLatin1(name).toLower()));
}

// Names the required files a folder is missing; empty means it's playable.
QStringList missingGameFiles(const QString &dir)
{
    QStringList missing;
    for(const char *name : REQUIRED_FILES)
    {
        if(!hasGameFile(dir, name))
            missing << QString::fromLatin1(name);
    }
    return missing;
}

// Finds the ARENA2 directory inside an extracted tree. Installers bury it at
// varying depths (DaggerfallSetup puts it under DF/DAGGER/ARENA2), so search
// for a marker file rather than assuming a layout, and prefer the shallowest
// match so a nested backup copy can't win over the real one.
QString findArena2(const QString &root)
{
    QString best;
    int bestDepth = -1;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    // The root itself may already be the game folder.
    if(hasGameFile(root, "ARCH3D.BSA"))
        return root;
    while(it.hasNext())
    {
        const QString dir = it.next();
        if(!hasGameFile(dir, "ARCH3D.BSA"))
            continue;
        const int depth = dir.count(QLatin1Char('/'));
        if(bestDepth < 0 || depth < bestDepth)
        {
            best = dir;
            bestDepth = depth;
        }
    }
    return best;
}

// How long to allow an unpack/install to run. DaggerfallSetup writes ~500 MB
// of small files; minutes on a slow disk, but never half an hour.
constexpr int EXTRACT_TIMEOUT_MS = 30 * 60 * 1000;

// Runs prog with args, returning false on spawn failure, timeout, or a
// non-zero exit. Combined stdout+stderr lands in *output either way.
//
// If a progress dialog is passed the Qt event loop keeps running while the
// child works, so the dialog paints and its Cancel button stays live; cancel
// kills the child and returns false. Without one this simply blocks.
bool runTool(const QString &prog, const QStringList &args, QString *output,
             int timeoutMs = EXTRACT_TIMEOUT_MS, QProgressDialog *progress = nullptr)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(prog, args);
    if(!p.waitForStarted(30 * 1000))
    {
        *output = MainWindow::tr("%1 could not be started.").arg(prog);
        return false;
    }

    if(!progress)
    {
        if(!p.waitForFinished(timeoutMs))
        {
            p.kill();
            p.waitForFinished(5000);
            *output = MainWindow::tr("%1 did not finish in time.").arg(prog);
            return false;
        }
        *output = QString::fromLocal8Bit(p.readAll()).trimmed();
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }

    QByteArray collected;
    QElapsedTimer elapsed;
    elapsed.start();
    while(p.state() != QProcess::NotRunning)
    {
        // Short slices: long enough not to spin the CPU, short enough that
        // Cancel feels immediate.
        p.waitForFinished(100);
        collected += p.readAll();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents
                                        | QEventLoop::AllEvents, 50);
        if(progress->wasCanceled())
        {
            p.kill();
            p.waitForFinished(5000);
            *output = MainWindow::tr("Cancelled.");
            return false;
        }
        if(elapsed.hasExpired(timeoutMs))
        {
            p.kill();
            p.waitForFinished(5000);
            *output = MainWindow::tr("%1 did not finish in time.").arg(prog);
            return false;
        }
    }
    collected += p.readAll();
    *output = QString::fromLocal8Bit(collected).trimmed();
    return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

// The Inno Setup version an installer was built with, e.g. "6.6.1". Read
// straight out of the file: Inno stores a literal "Inno Setup Setup Data
// (X.Y.Z)" marker in the setup header. Empty if no marker is found.
//
// This matters because innoextract refuses anything newer than it knows
// about, and every *released* innoextract (1.9, from 2020) stops at Inno
// 6.0.5 -- while current DaggerfallSetup builds use 6.6.x. Knowing both
// numbers lets us say why it failed instead of relaying a blank refusal.
QString innoSetupVersion(const QString &exePath)
{
    QFile f(exePath);
    if(!f.open(QIODevice::ReadOnly))
        return {};

    static const QByteArray marker = QByteArrayLiteral("Inno Setup Setup Data (");
    // The marker sits in the header, but well past the PE stub; scan the
    // first few MB in overlapping chunks so it can't fall across a boundary.
    constexpr qint64 CHUNK = 1 << 20;
    constexpr qint64 MAX_SCAN = 24 * CHUNK;
    QByteArray prev;
    for(qint64 read = 0; read < MAX_SCAN; read += CHUNK)
    {
        const QByteArray chunk = f.read(CHUNK);
        if(chunk.isEmpty())
            break;
        const QByteArray window = prev + chunk;
        const int at = window.indexOf(marker);
        if(at >= 0)
        {
            const int start = at + marker.size();
            const int end = window.indexOf(')', start);
            if(end > start)
                return QString::fromLatin1(window.mid(start, end - start));
            return {};
        }
        prev = chunk.right(marker.size());
    }
    return {};
}

// Highest Inno Setup version the installed innoextract admits to handling.
// It prints "Extracts installers created by Inno Setup <lo> to <hi>".
QString innoextractMaxVersion(const QString &innoExe)
{
    QString out;
    if(!runTool(innoExe, {QStringLiteral("--version")}, &out, 30 * 1000))
        return {};
    static const QRegularExpression re(
        QStringLiteral("Inno Setup [0-9.]+ to ([0-9.]+)"));
    const auto m = re.match(out);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Numeric compare of dotted versions ("6.6.1" > "6.0.5"). Missing components
// count as zero, so "6.1" and "6.1.0" compare equal.
int compareVersions(const QString &a, const QString &b)
{
    const QStringList as = a.split(QLatin1Char('.'));
    const QStringList bs = b.split(QLatin1Char('.'));
    for(int i = 0; i < qMax(as.size(), bs.size()); ++i)
    {
        const int av = i < as.size() ? as.at(i).toInt() : 0;
        const int bv = i < bs.size() ? bs.at(i).toInt() : 0;
        if(av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

// Locates innoextract, preferring a copy shipped beside the launcher (built
// with -DBUILD_INNOEXTRACT=ON) over whatever is on PATH: distro packages are
// still 1.9, which cannot read the Inno Setup 6.6 installers DaggerfallSetup
// now uses. Empty if neither exists.
QString findInnoextract()
{
    QString name = QStringLiteral("innoextract");
#ifdef Q_OS_WIN
    name += QStringLiteral(".exe");
#endif
    const QString bundled = QCoreApplication::applicationDirPath()
                          + QLatin1Char('/') + name;
    if(QFileInfo(bundled).isExecutable())
        return bundled;
    return QStandardPaths::findExecutable(QStringLiteral("innoextract"));
}

QString userConfigDir()
{
    // Mirrors the engine's own lookup (engine.cpp): $XDG_CONFIG_HOME, else
    // $HOME/.config, and %AppData% on Windows -- which is exactly what Qt's
    // AppConfigLocation resolves to, minus the app-name suffix Qt appends.
#ifdef Q_OS_WIN
    const QString base = qEnvironmentVariable("AppData");
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if(base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
#endif
    return base + QStringLiteral("/opendf");
}

// Where extracted game data goes when the launcher installs it.
QString defaultInstallRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
         + QStringLiteral("/daggerfall");
}

} // namespace


MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(tr("OpenDF Launcher"));

    // --- Game data -------------------------------------------------------
    mPathEdit = new QLineEdit(this);
    mPathEdit->setPlaceholderText(tr("Path to your Daggerfall ARENA2 folder"));
    auto *browseBtn = new QPushButton(tr("Browse…"), this);
    connect(browseBtn, &QPushButton::clicked, this, &MainWindow::browsePath);

    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(mPathEdit, 1);
    pathRow->addWidget(browseBtn);

    mPathStatus = new QLabel(this);
    mPathStatus->setWordWrap(true);

    auto *installBtn = new QPushButton(tr("Install from a downloaded archive…"), this);
    installBtn->setToolTip(tr("Unpack DaggerfallSetup (.zip or .exe) or a zipped "
                              "Daggerfall folder into a local copy."));
    connect(installBtn, &QPushButton::clicked, this, &MainWindow::installFromArchive);

    auto *getBtn = new QPushButton(tr("Where do I get the game?"), this);
    connect(getBtn, &QPushButton::clicked, this, &MainWindow::showGetGameDialog);

    auto *dataBox = new QGroupBox(tr("Game data"), this);
    auto *dataLayout = new QVBoxLayout(dataBox);
    dataLayout->addLayout(pathRow);
    dataLayout->addWidget(mPathStatus);
    dataLayout->addSpacing(4);
    dataLayout->addWidget(installBtn);
    dataLayout->addWidget(getBtn);

    // --- Video -----------------------------------------------------------
    mResolution = new QComboBox(this);
    for(const auto &wh : {QPair<int,int>{1280,720}, {1366,768}, {1600,900},
                          {1920,1080}, {2560,1440}, {3840,2160}})
    {
        mResolution->addItem(QStringLiteral("%1 × %2").arg(wh.first).arg(wh.second),
                             QPoint(wh.first, wh.second));
    }
    mFullscreen = new QCheckBox(tr("Fullscreen"), this);

    auto *videoBox = new QGroupBox(tr("Video"), this);
    auto *videoForm = new QFormLayout(videoBox);
    videoForm->addRow(tr("Resolution:"), mResolution);
    videoForm->addRow(QString(), mFullscreen);

    // --- Advanced --------------------------------------------------------
    mDevparm = new QCheckBox(tr("Verbose debug logging (-devparm)"), this);
    auto *advBox = new QGroupBox(tr("Advanced"), this);
    auto *advLayout = new QVBoxLayout(advBox);
    advLayout->addWidget(mDevparm);

    // --- Buttons ---------------------------------------------------------
    mPlayBtn = new QPushButton(tr("Play ▶"), this);
    mPlayBtn->setDefault(true);
    connect(mPlayBtn, &QPushButton::clicked, this, &MainWindow::play);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(mPlayBtn);

    auto *main = new QVBoxLayout(this);
    main->addWidget(dataBox);
    main->addWidget(videoBox);
    main->addWidget(advBox);
    main->addStretch(1);
    main->addLayout(buttons);

    loadSettings();

    connect(mPathEdit, &QLineEdit::textChanged, this, &MainWindow::validatePath);
    // Persist on focus-leave/Enter rather than per keystroke -- typing a long
    // path by hand shouldn't hit QSettings once per character.
    connect(mPathEdit, &QLineEdit::editingFinished, this, [this]{ saveSettings(); });
    connect(mResolution, &QComboBox::currentIndexChanged, this, [this](int){ saveSettings(); });
    connect(mFullscreen, &QCheckBox::toggled, this, [this](bool){ saveSettings(); });
    connect(mDevparm, &QCheckBox::toggled, this, [this](bool){ saveSettings(); });

    validatePath();
    setMinimumWidth(WINDOW_W);
    resize(WINDOW_W, 460);
}


void MainWindow::browsePath()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        tr("Locate your Daggerfall ARENA2 folder"), mPathEdit->text());
    if(dir.isEmpty())
        return;

    // Point people at ARENA2 when they pick its parent -- selecting the
    // Daggerfall install root instead of ARENA2 is the obvious mistake.
    QString chosen = dir;
    if(!missingGameFiles(dir).isEmpty())
    {
        const QString nested = findArena2(dir);
        if(!nested.isEmpty())
            chosen = nested;
    }
    mPathEdit->setText(chosen);
    saveSettings(); // editingFinished doesn't fire on programmatic setText
}


void MainWindow::validatePath()
{
    const QString dir = mPathEdit->text();
    if(dir.isEmpty())
    {
        mPathStatus->setText(tr("<span style='color:#a00'>Pick your ARENA2 folder, "
                                "or install the game below.</span>"));
        mPlayBtn->setEnabled(false);
        return;
    }

    const QStringList missing = missingGameFiles(dir);
    if(missing.isEmpty())
    {
        mPathStatus->setText(tr("<span style='color:#080'>✓ Daggerfall data found.</span>"));
        mPlayBtn->setEnabled(true);
        return;
    }

    // ARCH3D.BSA/DAGGER.SND missing while the rest are present is the
    // signature of an unpacked CD image: those two live inside PACKED.DAT
    // and only appear once the original installer has run.
    const bool looksLikeCd = !missing.contains(QStringLiteral("MAPS.BSA"))
                          && !missing.contains(QStringLiteral("BLOCKS.BSA"));
    if(looksLikeCd)
    {
        mPathStatus->setText(tr(
            "<span style='color:#a00'>Missing %1.</span><br>"
            "This looks like an <b>un-installed CD image</b> — those files are "
            "packed inside PACKED.DAT and only appear after the game has been "
            "installed. Use DaggerfallSetup (see “Where do I get the game?”).")
            .arg(missing.join(QStringLiteral(", "))));
    }
    else
    {
        mPathStatus->setText(tr("<span style='color:#a00'>Not a Daggerfall data "
                                "folder — missing %1.</span>")
                             .arg(missing.join(QStringLiteral(", "))));
    }
    mPlayBtn->setEnabled(false);
}


void MainWindow::showGetGameDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Getting Daggerfall"));
    auto *v = new QVBoxLayout(&dlg);

    auto *intro = new QLabel(tr(
        "OpenDF ships no game data. Daggerfall is <b>free</b>, and every source "
        "below is legitimate — pick whichever suits you."), &dlg);
    intro->setWordWrap(true);
    v->addWidget(intro);
    v->addSpacing(8);

    struct Source {
        const char *title;
        const char *blurb;
        const char *button;
        const char *url;
    };
    // Ordered by how well each one ends in a folder OpenDF can actually read.
    const Source sources[] = {
        {QT_TR_NOOP("DaggerfallSetup (recommended)"),
         QT_TR_NOOP("A pre-configured installer that produces a complete, "
                    "ready-to-use install. This is the one the launcher can "
                    "unpack for you — download it, then use “Install from a "
                    "downloaded archive…”."),
         QT_TR_NOOP("Download DaggerfallSetup 3.2.0"), DFSETUP_URL},
        {QT_TR_NOOP("GOG"),
         QT_TR_NOOP("Free on GOG. Install it (or run the offline installer), "
                    "then point the launcher at the ARENA2 folder."),
         QT_TR_NOOP("Open the GOG page"), GOG_URL},
        {QT_TR_NOOP("Steam"),
         QT_TR_NOOP("Free on Steam. After installing, ARENA2 sits inside the "
                    "game's install folder."),
         QT_TR_NOOP("Open the Steam page"), STEAM_URL},
        {QT_TR_NOOP("Bethesda"),
         QT_TR_NOOP("Bethesda's own page for the free release."),
         QT_TR_NOOP("Open Bethesda's page"), BETHESDA_URL},
        {QT_TR_NOOP("UESP file archive"),
         QT_TR_NOOP("Hosts DaggerfallSetup and the original DFInstall.zip. Note "
                    "that DFInstall.zip is the raw CD image — it still needs the "
                    "DOS installer to unpack ARCH3D.BSA and DAGGER.SND, so it "
                    "will not work on its own."),
         QT_TR_NOOP("Open the UESP file list"), UESP_URL},
    };

    for(const Source &s : sources)
    {
        auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(tr(s.title)), &dlg);
        v->addWidget(title);
        auto *blurb = new QLabel(tr(s.blurb), &dlg);
        blurb->setWordWrap(true);
        v->addWidget(blurb);
        auto *btn = new QPushButton(tr(s.button), &dlg);
        const QString url = QString::fromLatin1(s.url);
        connect(btn, &QPushButton::clicked, [url]{ QDesktopServices::openUrl(QUrl(url)); });
        v->addWidget(btn);
        v->addSpacing(10);
    }

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);
    dlg.exec();
}


void MainWindow::installFromArchive()
{
    const QString archive = QFileDialog::getOpenFileName(this,
        tr("Pick the archive or installer you downloaded"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tr("Daggerfall installers and archives (*.zip *.exe);;All files (*)"));
    if(archive.isEmpty())
        return;

    const QString destRoot = defaultInstallRoot();
    if(!findArena2(destRoot).isEmpty())
    {
        const auto pick = QMessageBox::warning(this, tr("Overwrite existing install?"),
            tr("Game data is already installed at:\n%1\n\nInstalling again will "
               "overwrite it. Continue?").arg(destRoot),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if(pick != QMessageBox::Yes)
            return;
    }

    // Unpacking DaggerfallSetup means half a gigabyte of small files and a
    // couple of minutes, so show real progress rather than freezing behind a
    // wait cursor. The steps aren't individually measurable, so this is a
    // busy indicator (range 0-0) whose label names the current stage.
    QProgressDialog progress(tr("Preparing…"), tr("Cancel"), 0, 0, this);
    progress.setWindowTitle(tr("Installing game data"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);

    QString err;
    const QString gameDir = installGameData(archive, destRoot, &err, &progress);
    const bool canceled = progress.wasCanceled();
    progress.close();

    if(gameDir.isEmpty())
    {
        if(canceled)
            return; // the user's own doing -- no need for an error box
        QMessageBox::critical(this, tr("Could not install the game data"), err);
        return;
    }

    mPathEdit->setText(gameDir);
    saveSettings();
    QMessageBox::information(this, tr("Game data installed"),
        tr("Daggerfall data installed to:\n%1\n\nThe launcher now points there — "
           "hit Play.").arg(gameDir));
}


QString MainWindow::installGameData(const QString &archivePath,
                                    const QString &destRoot, QString *err,
                                    QProgressDialog *progress)
{
    // Advances the dialog's label and keeps it painting during the long
    // synchronous stretches between child processes.
    const auto stage = [progress](const QString &text) {
        if(!progress)
            return;
        progress->setLabelText(text);
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents
                                        | QEventLoop::AllEvents, 50);
    };
    const auto canceled = [progress] { return progress && progress->wasCanceled(); };

    if(!QDir().mkpath(destRoot))
    {
        *err = tr("Could not create %1.").arg(destRoot);
        return {};
    }

    // DaggerfallSetup ships as a .zip wrapping a single Inno Setup .exe, so a
    // plain unzip only ever gets us one step closer. Unpack the zip first,
    // then hand whatever came out to innoextract if it's an installer.
    QTemporaryDir staging;
    if(!staging.isValid())
    {
        *err = tr("Could not create a temporary directory.");
        return {};
    }

    QString payload = archivePath;
    if(archivePath.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
    {
        stage(tr("Unpacking %1…").arg(QFileInfo(archivePath).fileName()));

        // unzip and 7z are near-universal on Linux/macOS; on Windows we lean
        // on PowerShell's Expand-Archive. Try each and use the first present.
        struct Unzipper { const char *prog; QStringList args; };
        const QList<Unzipper> unzippers = {
            {"unzip", {QStringLiteral("-o"), archivePath,
                       QStringLiteral("-d"), staging.path()}},
            {"7z",    {QStringLiteral("x"), QStringLiteral("-y"),
                       QStringLiteral("-o") + staging.path(), archivePath}},
            {"powershell", {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
                            QStringLiteral("Expand-Archive -LiteralPath '%1' "
                                           "-DestinationPath '%2' -Force")
                                .arg(archivePath, staging.path())}},
        };

        bool unzipped = false;
        QString lastOutput;
        for(const Unzipper &u : unzippers)
        {
            const QString exe = QStandardPaths::findExecutable(QString::fromLatin1(u.prog));
            if(exe.isEmpty())
                continue;
            if(runTool(exe, u.args, &lastOutput, EXTRACT_TIMEOUT_MS, progress))
            {
                unzipped = true;
                break;
            }
            // A cancel isn't a reason to go try the next unzipper.
            if(canceled())
                break;
        }
        if(canceled())
        {
            *err = tr("Cancelled.");
            return {};
        }
        if(!unzipped)
        {
            *err = tr("Could not unpack %1.\n\nNo working unzip tool was found "
                      "(tried unzip, 7z, PowerShell).%2")
                   .arg(QFileInfo(archivePath).fileName(),
                        lastOutput.isEmpty() ? QString()
                                             : QStringLiteral("\n\n") + lastOutput);
            return {};
        }

        // If the zip already held the game, we're done.
        const QString direct = findArena2(staging.path());
        if(!direct.isEmpty())
            payload = direct;
        else
        {
            // Otherwise look for the installer the zip was wrapping.
            QDirIterator it(staging.path(), {QStringLiteral("*.exe")}, QDir::Files,
                            QDirIterator::Subdirectories);
            if(!it.hasNext())
            {
                *err = tr("%1 unpacked, but it contained neither Daggerfall data "
                          "nor an installer.").arg(QFileInfo(archivePath).fileName());
                return {};
            }
            payload = it.next();
        }
    }

    // A directory here means the zip contained the game outright -- copy it in.
    if(QFileInfo(payload).isDir())
    {
        stage(tr("Copying game files…"));
        const QString dest = destRoot + QStringLiteral("/ARENA2");
        QDir().mkpath(dest);
        QDirIterator it(payload, QDir::Files);
        while(it.hasNext())
        {
            const QString src = it.next();
            const QString out = dest + QLatin1Char('/') + it.fileName();
            QFile::remove(out);
            if(!QFile::copy(src, out))
            {
                *err = tr("Could not copy %1 into %2.").arg(it.fileName(), dest);
                return {};
            }
        }
        return missingGameFiles(dest).isEmpty() ? dest : QString();
    }

    // An Inno Setup installer. Two ways to unpack it, tried in order:
    //
    //  1. innoextract -- fast, no Windows involved, but every released
    //     version (1.9, 2020) only understands Inno Setup up to 6.0.5, and
    //     current DaggerfallSetup builds are made with 6.6.x. So check the
    //     two version numbers up front rather than letting it fail with
    //     "Could not determine setup data version!".
    //  2. Wine, running the installer silently -- slower, but it copes with
    //     any Inno version because it's the real installer doing the work.
    //
    // Whichever runs, the result is validated the same way below.
    const QString fileName = QFileInfo(payload).fileName();
    const QString setupVersion = innoSetupVersion(payload);

    QString innoNote;   // why innoextract was skipped/failed, for the error text
    bool extracted = false;

    const QString inno = findInnoextract();
    if(inno.isEmpty())
    {
        innoNote = tr("innoextract is not installed.");
    }
    else
    {
        const QString maxVersion = innoextractMaxVersion(inno);
        if(!setupVersion.isEmpty() && !maxVersion.isEmpty()
           && compareVersions(setupVersion, maxVersion) > 0)
        {
            // Known-too-old: skip it rather than burning minutes on a run
            // that ends in a version complaint.
            innoNote = tr("The installed innoextract only handles Inno Setup up "
                          "to %1, but this installer was built with %2.")
                       .arg(maxVersion, setupVersion);
        }
        else
        {
            stage(tr("Extracting with innoextract…"));
            QString out;
            extracted = runTool(inno, {QStringLiteral("--extract"),
                                       QStringLiteral("--output-dir"), destRoot,
                                       QStringLiteral("--progress=0"),
                                       payload}, &out, EXTRACT_TIMEOUT_MS, progress);
            if(!extracted)
                innoNote = tr("innoextract failed:\n%1").arg(out);
        }
    }

    if(!extracted)
    {
        const QString wine = QStandardPaths::findExecutable(QStringLiteral("wine"));
        if(!wine.isEmpty())
        {
            // Silent install into a throwaway prefix inside destRoot, so we
            // never touch the user's own ~/.wine. /DIR must be a Windows path;
            // C:\DF maps to <prefix>/drive_c/DF.
            const QString prefix = destRoot + QStringLiteral("/.wineprefix");
            QDir(prefix).removeRecursively();
            QDir().mkpath(prefix);

            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("WINEPREFIX"), prefix);
            env.insert(QStringLiteral("WINEDEBUG"), QStringLiteral("-all"));

            stage(tr("Running the installer with Wine — this takes a few "
                     "minutes…"));

            QProcess p;
            p.setProcessEnvironment(env);
            p.setProcessChannelMode(QProcess::MergedChannels);
            p.start(wine, {payload,
                           QStringLiteral("/VERYSILENT"),
                           QStringLiteral("/SUPPRESSMSGBOXES"),
                           QStringLiteral("/NORESTART"),
                           QStringLiteral("/DIR=C:\\DF")});
            bool wineOk = p.waitForStarted(60 * 1000);
            if(wineOk)
            {
                QElapsedTimer wineElapsed;
                wineElapsed.start();
                while(p.state() != QProcess::NotRunning)
                {
                    p.waitForFinished(100);
                    QCoreApplication::processEvents(
                        QEventLoop::ExcludeUserInputEvents | QEventLoop::AllEvents, 50);
                    if(canceled() || wineElapsed.hasExpired(EXTRACT_TIMEOUT_MS))
                    {
                        p.kill();
                        p.waitForFinished(5000);
                        wineOk = false;
                        break;
                    }
                }
            }
            if(wineOk)
            {
                // Wine's exit code isn't a reliable success signal here, so
                // judge by whether the game data actually appeared.
                const QString installed = findArena2(prefix + QStringLiteral("/drive_c/DF"));
                if(!installed.isEmpty())
                {
                    // Lift the data out of the prefix, then bin the prefix --
                    // it's ~400 MB of Windows scaffolding we have no use for.
                    const QString dest = destRoot + QStringLiteral("/ARENA2");
                    QDir(dest).removeRecursively();
                    QDir().mkpath(dest);
                    stage(tr("Copying game files…"));
                    QDirIterator it(installed, QDir::Files);
                    bool copied = true;
                    int n = 0;
                    while(it.hasNext())
                    {
                        const QString src = it.next();
                        if(!QFile::copy(src, dest + QLatin1Char('/') + it.fileName()))
                        {
                            copied = false;
                            break;
                        }
                        // ~1500 files; repaint every so often, not per file.
                        if((++n % 100) == 0)
                            stage(tr("Copying game files… (%1)").arg(n));
                    }
                    stage(tr("Cleaning up…"));
                    QDir(prefix).removeRecursively();
                    extracted = copied;
                    if(!copied)
                        innoNote += tr("\n\nWine installed the game, but copying "
                                       "the files into %1 failed.").arg(dest);
                }
                else
                {
                    QDir(prefix).removeRecursively();
                    innoNote += tr("\n\nWine ran the installer but produced no "
                                   "game data.");
                }
            }
            else
            {
                p.kill();
                p.waitForFinished(5000);
                QDir(prefix).removeRecursively();
                innoNote += tr("\n\nWine did not finish in time.");
            }
        }
    }

    if(canceled())
    {
        *err = tr("Cancelled.");
        return {};
    }

    if(!extracted)
    {
        *err = tr("Could not unpack the Windows installer %1.\n\n%2\n\n"
                  "Ways forward:\n\n"
                  "• Rebuild OpenDF with <b>-DBUILD_INNOEXTRACT=ON</b> — that "
                  "builds a current innoextract next to the launcher, which "
                  "reads the Inno Setup 6.6 installers the packaged versions "
                  "cannot.\n\n"
                  "• Or install <b>Wine</b> and try again — the launcher will "
                  "run the installer with it automatically, which works with "
                  "any Inno Setup version.\n\n"
                  "• Or install the game yourself (GOG, Steam, or this "
                  "installer under Wine/Windows) and point the launcher at the "
                  "resulting ARENA2 folder.")
               .arg(fileName, innoNote.trimmed());
        return {};
    }

    const QString gameDir = findArena2(destRoot);
    if(gameDir.isEmpty())
    {
        *err = tr("Unpacked %1, but no Daggerfall data folder turned up inside "
                  "%2.").arg(QFileInfo(payload).fileName(), destRoot);
        return {};
    }
    const QStringList missing = missingGameFiles(gameDir);
    if(!missing.isEmpty())
    {
        *err = tr("Unpacked to %1, but these required files are missing: %2.\n\n"
                  "This archive probably holds the raw CD image rather than an "
                  "installed copy.").arg(gameDir, missing.join(QStringLiteral(", ")));
        return {};
    }
    return gameDir;
}


bool MainWindow::writeEngineConfig(QString *err) const
{
    const QString cfgDir = userConfigDir();
    if(!QDir().mkpath(cfgDir))
    {
        *err = tr("Could not create the config directory %1.").arg(cfgDir);
        return false;
    }

    // settings.cfg -- data-root plus the shipped data dirs. The engine reads
    // this and nothing on the command line can replace it.
    {
        const QString path = cfgDir + QStringLiteral("/settings.cfg");
        QFile f(path);
        if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            *err = tr("Could not write %1:\n%2").arg(path, f.errorString());
            return false;
        }
        QTextStream out(&f);
        out << "# Written by the OpenDF launcher. Edits are overwritten on Play.\n"
            << "data-root = " << QDir::toNativeSeparators(mPathEdit->text()) << "\n";

        // The GUI needs the shaders and MyGUI media that ship beside the
        // engine. In the build tree those sit in the source dir; once
        // installed they land next to (or one level up from) the binary.
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            appDir + QStringLiteral("/data"),
            appDir + QStringLiteral("/../data"),
            appDir + QStringLiteral("/../share/opendf/data"),
            QStringLiteral(OPENDF_DATA_DIR),
        };
        for(const QString &c : candidates)
        {
            if(!QFileInfo::exists(c + QStringLiteral("/shaders")))
                continue;
            const QString base = QDir(c).absolutePath();
            out << "data = " << base << "\n"
                << "data = " << base << "/MyGUI_Media\n";
            break;
        }
        f.close();
    }

    // opendf.cfg -- the [CVars] block the engine reads for video settings.
    // Preserve any other cvars the user has saved from the console; only the
    // three the launcher owns get rewritten.
    {
        const QString path = cfgDir + QStringLiteral("/opendf.cfg");
        QStringList preserved;
        QFile in(path);
        if(in.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream ts(&in);
            bool inCVars = false;
            while(!ts.atEnd())
            {
                const QString line = ts.readLine();
                const QString trimmed = line.trimmed();
                if(trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))
                {
                    inCVars = (trimmed.compare(QStringLiteral("[CVars]"),
                                               Qt::CaseInsensitive) == 0);
                    continue;
                }
                if(!inCVars || trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
                    continue;
                const QString key = trimmed.section(QLatin1Char('='), 0, 0).trimmed();
                if(key.compare(QStringLiteral("vid_width"), Qt::CaseInsensitive) == 0
                || key.compare(QStringLiteral("vid_height"), Qt::CaseInsensitive) == 0
                || key.compare(QStringLiteral("vid_fullscreen"), Qt::CaseInsensitive) == 0)
                    continue;
                preserved << trimmed;
            }
            in.close();
        }

        QFile f(path);
        if(!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            *err = tr("Could not write %1:\n%2").arg(path, f.errorString());
            return false;
        }
        const QPoint res = mResolution->currentData().toPoint();
        QTextStream out(&f);
        out << "# Written by the OpenDF launcher.\n\n[CVars]\n"
            << "vid_width = " << res.x() << "\n"
            << "vid_height = " << res.y() << "\n"
            << "vid_fullscreen = " << (mFullscreen->isChecked() ? "true" : "false") << "\n";
        for(const QString &line : preserved)
            out << line << "\n";
    }

    return true;
}


QString MainWindow::engineBinaryPath() const
{
    // Engine and launcher install side by side (see the top-level
    // CMakeLists.txt), and sit together in the build tree too.
    QString exe = QCoreApplication::applicationDirPath() + QStringLiteral("/opendf");
#ifdef Q_OS_WIN
    exe += QStringLiteral(".exe");
#endif
    return exe;
}


void MainWindow::play()
{
    const QString exe = engineBinaryPath();
    if(!QFileInfo::exists(exe))
    {
        QMessageBox::critical(this, tr("Engine not found"),
            tr("Could not find the opendf engine binary at:\n%1").arg(exe));
        return;
    }

    saveSettings();

    QString err;
    if(!writeEngineConfig(&err))
    {
        QMessageBox::critical(this, tr("Could not save configuration"), err);
        return;
    }

    QStringList args;
    if(mDevparm->isChecked())
        args << QStringLiteral("-devparm");

    if(!QProcess::startDetached(exe, args))
    {
        QMessageBox::critical(this, tr("Launch failed"),
            tr("Failed to start:\n%1").arg(exe));
        return;
    }
    QApplication::quit();
}


void MainWindow::loadSettings()
{
    QSettings s;
    mPathEdit->setText(s.value(QStringLiteral("gamePath")).toString());

    const int w = s.value(QStringLiteral("vidWidth"), 1280).toInt();
    const int h = s.value(QStringLiteral("vidHeight"), 720).toInt();
    int idx = mResolution->findData(QPoint(w, h));
    if(idx < 0)
    {
        // A resolution set by hand in opendf.cfg: keep it rather than
        // silently snapping the user back to a listed one.
        mResolution->addItem(QStringLiteral("%1 × %2").arg(w).arg(h), QPoint(w, h));
        idx = mResolution->count() - 1;
    }
    mResolution->setCurrentIndex(idx);

    mFullscreen->setChecked(s.value(QStringLiteral("vidFullscreen"), false).toBool());
    mDevparm->setChecked(s.value(QStringLiteral("devparm"), false).toBool());
}


void MainWindow::saveSettings() const
{
    QSettings s;
    s.setValue(QStringLiteral("gamePath"), mPathEdit->text());
    const QPoint res = mResolution->currentData().toPoint();
    s.setValue(QStringLiteral("vidWidth"), res.x());
    s.setValue(QStringLiteral("vidHeight"), res.y());
    s.setValue(QStringLiteral("vidFullscreen"), mFullscreen->isChecked());
    s.setValue(QStringLiteral("devparm"), mDevparm->isChecked());
}
