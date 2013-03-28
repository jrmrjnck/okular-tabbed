/***************************************************************************
 *   Copyright (C) 2002 by Wilco Greven <greven@kde.org>                   *
 *   Copyright (C) 2002 by Chris Cheney <ccheney@cheney.cx>                *
 *   Copyright (C) 2003 by Benjamin Meyer <benjamin@csh.rit.edu>           *
 *   Copyright (C) 2003-2004 by Christophe Devriese                        *
 *                         <Christophe.Devriese@student.kuleuven.ac.be>    *
 *   Copyright (C) 2003 by Laurent Montel <montel@kde.org>                 *
 *   Copyright (C) 2003-2004 by Albert Astals Cid <aacid@kde.org>          *
 *   Copyright (C) 2003 by Luboš Luňák <l.lunak@kde.org>                   *
 *   Copyright (C) 2003 by Malcolm Hunter <malcolm.hunter@gmx.co.uk>       *
 *   Copyright (C) 2004 by Dominique Devriese <devriese@kde.org>           *
 *   Copyright (C) 2004 by Dirk Mueller <mueller@kde.org>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "shell.h"

// qt/kde includes
#include <qdesktopwidget.h>
#include <qtimer.h>
#include <QtDBus/qdbusconnection.h>
#include <kaction.h>
#include <kapplication.h>
#include <kcmdlineargs.h>
#include <kfiledialog.h>
#include <kpluginloader.h>
#include <kmessagebox.h>
#include <kmimetype.h>
#include <kstandardaction.h>
#include <ktoolbar.h>
#include <kurl.h>
#include <kdebug.h>
#include <klocale.h>
#include <kmenubar.h>
#include <kio/netaccess.h>
#include <krecentfilesaction.h>
#include <kservicetypetrader.h>
#include <ktoggleaction.h>
#include <ktogglefullscreenaction.h>
#include <kactioncollection.h>
#include <kwindowsystem.h>
#include <ktabbar.h>
#include <QVBoxLayout>
#include <QStackedWidget>

#ifdef KActivities_FOUND
#include <KActivities/ResourceInstance>
#endif

// local includes
#include "kdocumentviewer.h"
#include "shellutils.h"

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

Shell::Shell(KCmdLineArgs* args)
  : KParts::MainWindow(), m_args(args), m_menuBarWasShown(true), m_toolBarWasShown(true)
#ifdef KActivities_FOUND
    , m_activityResource(0)
#endif
{
  init();
}

void Shell::init()
{
  setObjectName( QLatin1String( "okular::Shell" ) );
  setContextMenuPolicy( Qt::NoContextMenu );
  // set the shell's ui resource file
  setXMLFile("shell.rc");
  m_fileformatsscanned = false;
  m_showMenuBarAction = 0;
  // this routine will find and load our Part.  it finds the Part by
  // name which is a bad idea usually.. but it's alright in this
  // case since our Part is made for this Shell
  m_partFactory = KPluginLoader("okularpart").factory();
  if (!m_partFactory)
  {
    // if we couldn't find our Part, we exit since the Shell by
    // itself can't do anything useful
    KMessageBox::error(this, i18n("Unable to find the Okular component."));
    return;
  }

  // now that the Part plugin is loaded, create the part
  KParts::ReadWritePart* firstPart = m_partFactory->create< KParts::ReadWritePart >( this );
  if (firstPart)
  {
    // Setup tab bar
    m_tabBar = new KTabBar( this );
    m_tabBar->setMovable( true );
    m_tabBar->setTabsClosable( true );
    connect( m_tabBar, SIGNAL(currentChanged(int)), SLOT(setActiveTab(int)) );
    connect( m_tabBar, SIGNAL(tabCloseRequested(int)), SLOT(closeTab(int)) );
    connect( m_tabBar, SIGNAL(contextMenu(int,QPoint)), SLOT(openTabContextMenu(int,QPoint)) );
    connect( m_tabBar, SIGNAL(mouseMiddleClick(int)), SLOT(closeTab(int)) );
    connect( m_tabBar, SIGNAL(tabMoved(int,int)), SLOT(moveTab(int,int)) );

    QWidget* centralWidget = new QWidget( this );
    m_viewStack = new QStackedWidget( this );
    m_viewStack->addWidget( firstPart->widget() );

    m_centralLayout = new QVBoxLayout( centralWidget );
    m_centralLayout->setSpacing( 0 );
    m_centralLayout->setMargin( 0 );
    m_centralLayout->addWidget( m_tabBar );
    m_centralLayout->addWidget( m_viewStack );
    setCentralWidget( centralWidget );

    m_tabs.append( TabState(firstPart) );
    m_activeTab = 0;

    // then, setup our actions
    setupActions();
    // and integrate the part's GUI with the shell's
    setupGUI(Keys | ToolBar | Save);
    createGUI(firstPart);

    connectPart( firstPart );

    readSettings();

    if (m_args && m_args->isSet("unique") && m_args->count() == 1)
    {
        QDBusConnection::sessionBus().registerService("org.kde.okular");
    }

    m_unique = false;
    if (m_args && m_args->isSet("unique") && m_args->count() <= 1)
    {
        m_unique = QDBusConnection::sessionBus().registerService("org.kde.okular");
        if (!m_unique)
            KMessageBox::information(this, i18n("There is already a unique Okular instance running. This instance won't be the unique one."));
    }
    
    if (m_args && !m_args->isSet("raise"))
    {
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    
    QDBusConnection::sessionBus().registerObject("/okularshell", this, QDBusConnection::ExportScriptableSlots);

    if (m_args && m_args->count() > 0) QTimer::singleShot(0, this, SLOT(delayedOpen()));
  }
  else
  {
    KMessageBox::error(this, i18n("Unable to find the Okular component."));
  }
}

void Shell::delayedOpen()
{
    if( m_args )
    {
        for( int i = 0; i < m_args->count(); ++i )
        {
            KUrl url = ShellUtils::urlFromArg(m_args->arg(i),
                           ShellUtils::qfileExistFunc(), m_args->getOption("page"));
            if( url.isValid() )
                openUrl( url );
        }
    }
    if( m_activeTab > 0 )
        setActiveTab( 0 );
}

void Shell::showOpenRecentMenu()
{
    m_recent->menu()->popup(QCursor::pos());
}

Shell::~Shell()
{
    if( !m_tabs.empty() )
    {
        writeSettings();
        for( QList<TabState>::iterator it = m_tabs.begin(); it != m_tabs.end(); ++it )
        {
           it->part->closeUrl( false );
        }
    }
    if ( m_args )
        m_args->clear();
}

void Shell::openUrl( const KUrl & url )
{
    if( m_tabs.size() == 0 )
        return;

    if( !m_tabs[m_activeTab].part->url().isEmpty() )
    {
        if( m_unique )
        {
            KMessageBox::error(this, i18n("Can't open more than one document in the unique Okular instance."));
        }
        else
        {
            openNewTab( url );
        }
    }
    else
    {
        KParts::ReadWritePart* emptyPart = m_tabs[m_activeTab].part;
        if ( m_args ){
            KDocumentViewer* doc = qobject_cast<KDocumentViewer*>(emptyPart);
            if ( doc && m_args->isSet( "presentation" ) )
                doc->startPresentation();
            if ( m_args->isSet( "print" ) )
                QMetaObject::invokeMethod( emptyPart, "enableStartWithPrint" );
        }
        bool openOk = emptyPart->openUrl( url );
        const bool isstdin = url.fileName( KUrl::ObeyTrailingSlash ) == QLatin1String( "-" );
        if ( !isstdin )
        {
            if ( openOk )
            {
#ifdef KActivities_FOUND
                if ( !m_activityResource )
                    m_activityResource = new KActivities::ResourceInstance( window()->winId(), this );

                m_activityResource->setUri( url );
#endif
                m_recent->addUrl( url );
            }
            else
                m_recent->removeUrl( url );
        }
    }
}

void Shell::closeUrl()
{
    closeTab( m_activeTab );
}

void Shell::readSettings()
{
    m_recent->loadEntries( KGlobal::config()->group( "Recent Files" ) );
    m_recent->setEnabled( true ); // force enabling

    const KConfigGroup group = KGlobal::config()->group( "Desktop Entry" );
    bool fullScreen = group.readEntry( "FullScreen", false );
    setFullScreen( fullScreen );
    
    if (fullScreen)
    {
        m_menuBarWasShown = group.readEntry( shouldShowMenuBarComingFromFullScreen, true );
        m_toolBarWasShown = group.readEntry( shouldShowToolBarComingFromFullScreen, true );
    }
}

void Shell::writeSettings()
{
    m_recent->saveEntries( KGlobal::config()->group( "Recent Files" ) );
    KConfigGroup group = KGlobal::config()->group( "Desktop Entry" );
    group.writeEntry( "FullScreen", m_fullScreenAction->isChecked() );
    if (m_fullScreenAction->isChecked())
    {
        group.writeEntry( shouldShowMenuBarComingFromFullScreen, m_menuBarWasShown );
        group.writeEntry( shouldShowToolBarComingFromFullScreen, m_toolBarWasShown );
    }
    KGlobal::config()->sync();
}

void Shell::setupActions()
{
  KStandardAction::open(this, SLOT(fileOpen()), actionCollection());
  m_recent = KStandardAction::openRecent( this, SLOT(openUrl(KUrl)), actionCollection() );
  m_recent->setToolBarMode( KRecentFilesAction::MenuMode );
  connect( m_recent, SIGNAL(triggered()), this, SLOT(showOpenRecentMenu()) );
  m_recent->setToolTip( i18n("Click to open a file\nClick and hold to open a recent file") );
  m_recent->setWhatsThis( i18n( "<b>Click</b> to open a file or <b>Click and hold</b> to select a recent file" ) );
  m_printAction = KStandardAction::print( this, SLOT(print()), actionCollection() );
  m_printAction->setEnabled( false );
  m_closeAction = KStandardAction::close( this, SLOT(closeUrl()), actionCollection() );
  m_closeAction->setEnabled( false );
  KStandardAction::quit(this, SLOT(slotQuit()), actionCollection());

  setStandardToolBarMenuEnabled(true);

  m_showMenuBarAction = KStandardAction::showMenubar( this, SLOT(slotShowMenubar()), actionCollection());
  m_fullScreenAction = KStandardAction::fullScreen( this, SLOT(slotUpdateFullScreen()), this,actionCollection() );
}

void Shell::saveProperties(KConfigGroup &group)
{
  // the 'config' object points to the session managed
  // config file.  anything you write here will be available
  // later when this app is restored
    emit saveDocumentRestoreInfo(group);
}

void Shell::readProperties(const KConfigGroup &group)
{
  // the 'config' object points to the session managed
  // config file.  this function is automatically called whenever
  // the app is being restored.  read in here whatever you wrote
  // in 'saveProperties'
    emit restoreDocument(group);
}

QStringList Shell::fileFormats() const
{
    QStringList supportedPatterns;

    QString constraint( "(Library == 'okularpart')" );
    QLatin1String basePartService( "KParts/ReadOnlyPart" );
    KService::List offers = KServiceTypeTrader::self()->query( basePartService, constraint );
    KService::List::ConstIterator it = offers.constBegin(), itEnd = offers.constEnd();
    for ( ; it != itEnd; ++it )
    {
        KService::Ptr service = *it;
        QStringList mimeTypes = service->serviceTypes();
        foreach ( const QString& mimeType, mimeTypes )
            if ( mimeType != basePartService )
                supportedPatterns.append( mimeType );
    }

    return supportedPatterns;
}

void Shell::fileOpen()
{
	// this slot is called whenever the File->Open menu is selected,
	// the Open shortcut is pressed (usually CTRL+O) or the Open toolbar
	// button is clicked
    if ( !m_fileformatsscanned )
    {
        KDocumentViewer* doc = qobject_cast<KDocumentViewer*>(m_tabs[m_activeTab].part);
        if ( doc )
            m_fileformats = doc->supportedMimeTypes();

        if ( m_fileformats.isEmpty() )
            m_fileformats = fileFormats();

        m_fileformatsscanned = true;
    }

    QString startDir;
    KParts::ReadWritePart* curPart = m_tabs[m_activeTab].part;
    if ( curPart->url().isLocalFile() )
        startDir = curPart->url().toLocalFile();
    KFileDialog dlg( startDir, QString(), this );
    dlg.setOperationMode( KFileDialog::Opening );

    // A directory may be a document. E.g. comicbook generator.
    if ( m_fileformats.contains( "inode/directory" ) )
        dlg.setMode( dlg.mode() | KFile::Directory );

    if ( m_fileformatsscanned && m_fileformats.isEmpty() )
        dlg.setFilter( i18n( "*|All Files" ) );
    else
        dlg.setMimeFilter( m_fileformats );
    dlg.setCaption( i18n( "Open Document" ) );
    if ( !dlg.exec() )
        return;
    KUrl url = dlg.selectedUrl();
    if ( !url.isEmpty() )
    {
        openUrl( url );
    }
}

void Shell::slotQuit()
{
    close();
}

void Shell::tryRaise()
{
    if (m_unique)
    {
        KWindowSystem::forceActiveWindow( window()->effectiveWinId() );
    }
}

// only called when starting the program
void Shell::setFullScreen( bool useFullScreen )
{
    if( useFullScreen )
        setWindowState( windowState() | Qt::WindowFullScreen ); // set
    else
        setWindowState( windowState() & ~Qt::WindowFullScreen ); // reset
}

void Shell::showEvent(QShowEvent *e)
{
    if (m_showMenuBarAction)
        m_showMenuBarAction->setChecked( menuBar()->isVisible() );

    KParts::MainWindow::showEvent(e);
}

void Shell::slotUpdateFullScreen()
{
    if(m_fullScreenAction->isChecked())
    {
      m_menuBarWasShown = !menuBar()->isHidden();
      menuBar()->hide();
      
      m_toolBarWasShown = !toolBar()->isHidden();
      toolBar()->hide();

      KToggleFullScreenAction::setFullScreen(this, true);      
    }
    else
    {
      if (m_menuBarWasShown)
      {
        menuBar()->show();
      }
      if (m_toolBarWasShown)
      {
        toolBar()->show();
      }
      KToggleFullScreenAction::setFullScreen(this, false);      
    }
}

void Shell::slotShowMenubar()
{
    if ( menuBar()->isHidden() )
        menuBar()->show();
    else
        menuBar()->hide();
}

QSize Shell::sizeHint() const
{
    return QApplication::desktop()->availableGeometry( this ).size() * 0.75;
}

bool Shell::queryClose()
{
    bool ret = true;
    for( QList<TabState>::iterator it = m_tabs.begin(); it != m_tabs.end(); ++it )
    {
        ret = ret && it->part->queryClose();
    }
    return ret;
}

void Shell::setActiveTab( int tab )
{
    if( tab == -1 )
        return;

    m_activeTab = tab;
    m_tabBar->setCurrentIndex( tab );
    m_viewStack->setCurrentWidget( m_tabs[tab].part->widget() );
    createGUI( m_tabs[tab].part );
}

void Shell::closeTab( int tab )
{
    m_viewStack->removeWidget( m_tabs[tab].part->widget() );
    m_tabs[tab].part->closeUrl();
    m_tabs[tab].part->disconnect();
    m_tabs[tab].part->deleteLater();
    m_tabs.removeAt( tab );

    m_tabBar->removeTab( tab );

    if( m_tabBar->count() == 1 )
        m_tabBar->removeTab( 0 );
}

void Shell::openTabContextMenu( int /*tab*/, QPoint /*point*/ )
{
}

void Shell::moveTab( int from, int to )
{
    m_tabs.move( from, to );
}

void Shell::openNewTab( const KUrl& url )
{
    // Tabs are hidden when there's only one, so show it
    if( m_tabs.size() == 1 )
    {
        m_tabBar->addTab( m_tabs[0].part->url().fileName() );
    }

    // Make new part
    m_tabs.append( m_partFactory->create<KParts::ReadWritePart>(this) );
    m_tabs.last().part->openUrl( url );
    connectPart( m_tabs.last().part );

    // Update GUI
    m_activeTab = m_tabs.size() - 1;
    m_viewStack->addWidget( m_tabs.last().part->widget() );
    m_viewStack->setCurrentIndex( m_activeTab );
    m_tabBar->addTab( m_tabs.last().part->url().fileName() );
    m_tabBar->setCurrentIndex( m_activeTab );
}

void Shell::connectPart( QObject* part )
{
    // These signals and slots defined in Okular::Part, but since we don't 
    // have access to that class, just pass in a KParts::Part* for brevity
    connect( this, SIGNAL(restoreDocument(KConfigGroup)), part, SLOT(restoreDocument(KConfigGroup)));
    connect( this, SIGNAL(saveDocumentRestoreInfo(KConfigGroup&)), part, SLOT(saveDocumentRestoreInfo(KConfigGroup&)));
    connect( part, SIGNAL(enablePrintAction(bool)), m_printAction, SLOT(setEnabled(bool)));
    connect( part, SIGNAL(enableCloseAction(bool)), m_closeAction, SLOT(setEnabled(bool)));
}

void Shell::print()
{
    QMetaObject::invokeMethod( m_tabs[m_activeTab].part, SLOT(slotPrint()) );
}

#include "shell.moc"

/* kate: replace-tabs on; indent-width 4; */
