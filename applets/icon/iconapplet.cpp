/*
 * Copyright 2016 Kai Uwe Broulik <kde@privat.broulik.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License or (at your option) version 3 or any later version
 * accepted by the membership of KDE e.V. (or its successor approved
 * by the membership of KDE e.V.), which shall act as a proxy
 * defined in Section 14 of version 3 of the license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "iconapplet.h"

#include <QAction>
#include <QApplication>
#include <QDesktopWidget>
#include <QDir>
#include <QDropEvent>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProcess>

#include <KAuthorized>
#include <KDesktopFile>
#include <KFileItemActions>
#include <KFileItemListProperties>
#include <KJobWidgets>
#include <KLocalizedString>
#include <KProtocolManager>
#include <KRun>

#include <KIO/DropJob>
#include <KIO/OpenFileManagerWindowJob>
#include <KIO/StatJob>

IconApplet::IconApplet(QObject *parent, const QVariantList &data)
    : Plasma::Applet(parent, data)
{

}

IconApplet::~IconApplet()
{
    // in a handler connected to IconApplet::appletDeleted m_localPath will be empty?!
    if (destroyed()) {
        QFile::remove(m_localPath);
    }
}

void IconApplet::init()
{
    populate();
}

void IconApplet::configChanged()
{
    populate();
}

void IconApplet::populate()
{
    m_url = config().readEntry(QStringLiteral("url"), QUrl());

    if (!m_url.isValid()) {
        // the old applet that used a QML plugin and stored its url
        // in plasmoid.configuration.url had its entries stored in [Configuration][General]
        // so we look here as well to provide an upgrade path
        m_url = config().group("General").readEntry(QStringLiteral("url"), QUrl());
    }

    // our backing desktop file already exists? just read all the things from it
    const QString path = localPath();
    if (QFileInfo::exists(path)) {
        populateFromDesktopFile(path);
        return;
    }

    if (!m_url.isValid()) {
        // invalid url, use dummy data
        populateFromDesktopFile(QString());
        return;
    }

    const QString plasmaIconsFolderPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1String("/plasma_icons");
    if (!QDir().mkpath(plasmaIconsFolderPath)) {
        setLaunchErrorMessage(i18n("Failed to create icon widgets folder '%1'", plasmaIconsFolderPath));
        return;
    }

    setBusy(true); // unset in populateFromDesktopFile where we'll end up in if all goes well

    auto *statJob = KIO::stat(m_url, KIO::HideProgressInfo);
    connect(statJob, &KIO::StatJob::finished, this, [=] {
        QString desiredDesktopFileName = m_url.fileName();

        // in doubt, just encode the entire URL, e.g. http://www.kde.org/ has no filename
        if (desiredDesktopFileName.isEmpty()) {
            desiredDesktopFileName = KIO::encodeFileName(m_url.toDisplayString());
        }

        // We always want it to be a .desktop file (e.g. also for the "Type=Link" at the end)
        if (!desiredDesktopFileName.endsWith(QLatin1String(".desktop"))) {
            desiredDesktopFileName.append(QLatin1String(".desktop"));
        }

        QString backingDesktopFile = plasmaIconsFolderPath + QLatin1Char('/');
        // KIO::suggestName always appends a suffix, i.e. it expects that we already know the file already exists
        if (QFileInfo::exists(backingDesktopFile + desiredDesktopFileName)) {
            desiredDesktopFileName = KIO::suggestName(QUrl::fromLocalFile(plasmaIconsFolderPath), desiredDesktopFileName);
        }
        backingDesktopFile.append(desiredDesktopFileName);

        QString name; // ends up as "Name" in the .desktop file for "Link" files below

        const QUrl url = statJob->mostLocalUrl();
        if (url.isLocalFile()) {
            const QString localUrlString = url.toLocalFile();

            // if desktop file just copy it over
            if (KDesktopFile::isDesktopFile(localUrlString)) {
                // if this restriction is set, KIO won't allow running desktop files from outside
                // registered services, applications, and so on, in this case we'll use the original
                // .desktop file and lose the ability to customize it
                if (!KAuthorized::authorize(QStringLiteral("run_desktop_files"))) {
                    populateFromDesktopFile(localUrlString);
                    // we don't call setLocalPath here as we don't want to store localPath to be a system-location
                    // so that the fact that we cannot edit is re-evaluated every time
                    return;
                }

                if (!QFile::copy(localUrlString, backingDesktopFile)) {
                    setLaunchErrorMessage(i18n("Failed to copy icon widget desktop file from '%1' to '%2'", localUrlString, backingDesktopFile));
                    return;
                }

                // set executable flag on the desktop file so KIO doesn't complain about executing it
                QFile file(backingDesktopFile);
                file.setPermissions(file.permissions() | QFile::ExeOwner);

                populateFromDesktopFile(backingDesktopFile);
                setLocalPath(backingDesktopFile);

                return;
            }
        }

        // in all other cases just make it a link

        QString iconName;
        QString genericName;

        if (!statJob->error()) {
            KFileItem item(statJob->statResult(), url);

            if (name.isEmpty()) {
                name = item.text();
            }

            if (item.mimetype() != QLatin1String("application/octet-stream")) {
                iconName = item.iconName();
                genericName = item.mimeComment();
            }
        }

        // KFileItem might return "." as text for e.g. root folders
        if (name == QLatin1String(".")) {
            name.clear();
        }

        if (name.isEmpty()) {
            name = url.fileName();
        }

        if (name.isEmpty()) {
            // TODO would be cool to just show the parent folder name instead of the full path
            name = url.path();
        }

        // For websites the filename e.g. "index.php" is usually not what you want
        // also "/" isn't very descript when it's not our local "root" folder
        if (name.isEmpty() || url.scheme().startsWith(QLatin1String("http")) || (!url.isLocalFile() && name == QLatin1String("/"))) {
            name = url.host();
        }

        if (iconName.isEmpty()) {
            // In doubt ask KIO::iconNameForUrl, KFileItem can't cope with http:// URLs for instance
            iconName = KIO::iconNameForUrl(url);
        }

        KDesktopFile linkDesktopFile(backingDesktopFile);
        auto desktopGroup = linkDesktopFile.desktopGroup();

        desktopGroup.writeEntry(QStringLiteral("Name"), name);
        desktopGroup.writeEntry(QStringLiteral("Type"), QStringLiteral("Link"));
        desktopGroup.writeEntry(QStringLiteral("URL"), url);
        desktopGroup.writeEntry(QStringLiteral("Icon"), iconName);
        if (!genericName.isEmpty()) {
            desktopGroup.writeEntry(QStringLiteral("GenericName"), genericName);
        }

        linkDesktopFile.sync();

        populateFromDesktopFile(backingDesktopFile);
        setLocalPath(backingDesktopFile);
    });
}

void IconApplet::populateFromDesktopFile(const QString &path)
{
    // path empty? just set icon to "unknown" and call it a day
    if (path.isEmpty()) {
        setIconName({});
        return;
    }

    KDesktopFile desktopFile(path);

    const QString &name = desktopFile.readName();
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
    }

    const QString &genericName = desktopFile.readGenericName();
    if (m_genericName != genericName) {
        m_genericName = genericName;
        emit genericNameChanged(genericName);
    }

    setIconName(desktopFile.readIcon());

    delete m_openContainingFolderAction;
    m_openContainingFolderAction = nullptr;
    m_openWithActions.clear();

    if (desktopFile.hasLinkType()) {
        const QUrl &linkUrl = QUrl(desktopFile.readUrl());

        if (!m_fileItemActions) {
            m_fileItemActions = new KFileItemActions(this);
        }
        KFileItemListProperties itemProperties(KFileItemList({KFileItem(linkUrl)}));
        m_fileItemActions->setItemListProperties(itemProperties);

        if (!m_openWithMenu) {
            m_openWithMenu.reset(new QMenu());
        }
        m_openWithMenu->clear();
        m_fileItemActions->addOpenWithActionsTo(m_openWithMenu.data());

        m_openWithActions = m_openWithMenu->actions();

        if (KProtocolManager::supportsListing(linkUrl)) {
            if (!m_openContainingFolderAction) {
                m_openContainingFolderAction = new QAction(QIcon::fromTheme(QStringLiteral("document-open-folder")), i18n("Open Containing Folder"), this);
                connect(m_openContainingFolderAction, &QAction::triggered, this, [this] {
                    KIO::highlightInFileManager({m_openContainingFolderAction->property("linkUrl").toUrl()});
                });
            }
            m_openContainingFolderAction->setProperty("linkUrl", linkUrl);
        }
    }

    m_jumpListActions.clear();
    foreach (const QString &actionName, desktopFile.readActions()) {
        const KConfigGroup &actionGroup = desktopFile.actionGroup(actionName);

        if (!actionGroup.isValid() || !actionGroup.exists()) {
            continue;
        }

        const QString &name = actionGroup.readEntry(QStringLiteral("Name"));
        const QString &exec = actionGroup.readEntry(QStringLiteral("Exec"));
        if (name.isEmpty() || exec.isEmpty()) {
            continue;
        }

        QAction *action = new QAction(QIcon::fromTheme(actionGroup.readEntry("Icon")), name, this);
        connect(action, &QAction::triggered, this, [this, exec] {
            KRun::run(exec, {}, nullptr, m_name, m_iconName);
        });

        m_jumpListActions << action;
    }

    m_localPath = path;

    setBusy(false);
}

QUrl IconApplet::url() const
{
    return m_url;
}

void IconApplet::setUrl(const QUrl &url)
{
    if (m_url != url) {
        m_url = url;
        urlChanged(url);

        config().writeEntry(QStringLiteral("url"), url);

        populate();
    }
}

void IconApplet::setIconName(const QString &iconName)
{
    const QString newIconName = (!iconName.isEmpty() ? iconName : QStringLiteral("unknown"));
    if (m_iconName != newIconName) {
        m_iconName = newIconName;
        emit iconNameChanged(newIconName);
    }
}

QString IconApplet::name() const
{
    return m_name;
}

QString IconApplet::iconName() const
{
    return m_iconName;
}

QString IconApplet::genericName() const
{
    return m_genericName;
}

QList<QAction *> IconApplet::contextualActions()
{
    QList<QAction *> actions;
    actions << m_jumpListActions;

    if (!actions.isEmpty()) {
        if (!m_separatorAction) {
            m_separatorAction = new QAction(this);
            m_separatorAction->setSeparator(true);
        }
        actions << m_separatorAction;
    }

    actions << m_openWithActions;

    if (m_openContainingFolderAction) {
        actions << m_openContainingFolderAction;
    }

    return actions;
}

void IconApplet::run()
{
    new KRun(QUrl::fromLocalFile(m_localPath), QApplication::desktop());
}

void IconApplet::processDrop(QObject *dropEvent)
{
    Q_ASSERT(dropEvent);
    Q_ASSERT(isAcceptableDrag(dropEvent));

    const auto &urls = urlsFromDrop(dropEvent);

    if (urls.isEmpty()) {
        return;
    }

    const QString &localPath = m_url.toLocalFile();

    if (KDesktopFile::isDesktopFile(localPath)) {
        KRun::runService(KService(localPath), urls, nullptr);
        return;
    }

    QMimeDatabase db;
    const QMimeType mimeType = db.mimeTypeForUrl(m_url);

    if (isExecutable(mimeType)) { // isAcceptableDrag has the KAuthorized check for this
        QProcess::startDetached(m_url.toLocalFile(), QUrl::toStringList(urls));
        return;
    }

    if (mimeType.inherits(QStringLiteral("inode/directory"))) {
        QMimeData mimeData;
        mimeData.setUrls(urls);

        // DeclarativeDropEvent isn't public
        QDropEvent de(QPointF(dropEvent->property("x").toInt(), dropEvent->property("y").toInt()),
                      static_cast<Qt::DropActions>(dropEvent->property("proposedActions").toInt()),
                      &mimeData,
                      static_cast<Qt::MouseButtons>(dropEvent->property("buttons").toInt()),
                      static_cast<Qt::KeyboardModifiers>(dropEvent->property("modifiers").toInt()));

        KIO::DropJob *dropJob = KIO::drop(&de, m_url);
        KJobWidgets::setWindow(dropJob, QApplication::desktop());
        return;
    }
}

bool IconApplet::isAcceptableDrag(QObject *dropEvent)
{
    Q_ASSERT(dropEvent);

    const auto &urls = urlsFromDrop(dropEvent);

    if (urls.isEmpty()) {
        return false;
    }

    const QString &localPath = m_url.toLocalFile();
    if (KDesktopFile::isDesktopFile(localPath)) {
        return true;
    }

    QMimeDatabase db;
    const QMimeType mimeType = db.mimeTypeForUrl(m_url);

    if (KAuthorized::authorize(QStringLiteral("shell_access")) && isExecutable(mimeType)) {
        return true;
    }

    if (mimeType.inherits(QStringLiteral("inode/directory"))) {
        return true;
    }

    return false;
}

QList<QUrl> IconApplet::urlsFromDrop(QObject *dropEvent)
{
    // DeclarativeDropEvent and co aren't public
    const QObject *mimeData = qvariant_cast<QObject *>(dropEvent->property("mimeData"));
    Q_ASSERT(mimeData);

    const QJsonArray &droppedUrls = mimeData->property("urls").toJsonArray();

    QList<QUrl> urls;
    urls.reserve(droppedUrls.count());
    for (const QJsonValue &droppedUrl : droppedUrls) {
        const QUrl url(droppedUrl.toString());
        if (url.isValid()) {
            urls.append(url);
        }
    }

    return urls;
}

bool IconApplet::isExecutable(const QMimeType &mimeType)
{
    return (mimeType.inherits(QStringLiteral("application/x-executable"))
            || mimeType.inherits(QStringLiteral("application/x-shellscript")));
}

void IconApplet::configure()
{
    KPropertiesDialog *dialog = m_configDialog.data();

    if (dialog) {
        dialog->show();
        dialog->raise();
        return;
    }

    dialog = new KPropertiesDialog(QUrl::fromLocalFile(m_localPath));
    m_configDialog = dialog;

    connect(dialog, &KPropertiesDialog::applied, this, [this] {
        KDesktopFile desktopFile(m_localPath);
        if (desktopFile.hasLinkType()) {
            // make sure to fully repopulate in case the user changed the Link URL
            QFile::remove(m_localPath);
            setUrl(QUrl(desktopFile.readUrl())); // calls populate() itself
        } else {
            populate();
        }
    });

    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setFileNameReadOnly(true);
    dialog->setWindowTitle(i18n("Properties for %1", m_name));
    dialog->setWindowIcon(QIcon::fromTheme(QStringLiteral("document-properties")));
    dialog->show();
}

QString IconApplet::localPath() const
{
    return config().readEntry(QStringLiteral("localPath"));
}

void IconApplet::setLocalPath(const QString &localPath)
{
    m_localPath = localPath;
    config().writeEntry(QStringLiteral("localPath"), localPath);
}

K_EXPORT_PLASMA_APPLET_WITH_JSON(icon, IconApplet, "metadata.json")

#include "iconapplet.moc"