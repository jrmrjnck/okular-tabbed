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
#include "kxmlguifactory.h"

#ifdef KActivities_FOUND
#include <KActivities/ResourceInstance>
#endif

// local includes
#include "kdocumentviewer.h"
#include "shellutils.h"

static const char *shouldShowMenuBarComingFromFullScreen = "shouldShowMenuBarComingFromFullScreen";
static const char *shouldShowToolBarComingFromFullScreen = "shouldShowToolBarComingFromFullScreen";

Shell::Shell(KCmdLineArgs* args, int argIndex)
  : KParts::MainWindow(), m_args(args), m_menuBarWasShown(true), m_toolBarWasShown(true)
#ifdef KActivities_FOUND
    , m_activityResource(0)
#endif
{
  if (m_args && argIndex != -1)
  {
    m_openUrl = ShellUtils::urlFromArg(m_args->arg(argIndex),
        ShellUtils::qfileExistFunc(), m_args->getOption("page"));
  }
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
  KParts::ReadWritePart* const firstPart = m_partFactory->create< KParts::ReadWritePart >( this );
  if (firstPart)
  {
    // Setup tab bar
    m_tabBar = new KTabBar( this );
    m_tabBar->setTabsClosable( true );
    m_tabBar->setElideMode( Qt::ElideRight );
    connect( m_tabBar, SIGNAL(currentChanged(int)), SLOT(setActiveTab(int)) );
    connect( m_tabBar, SIGNAL(tabCloseRequested(int)), SLOT(closeTab(int)) );

    QWidget* const centralWidget = new QWidget( this );
    m_viewStack = new QStackedWidget( this );
    m_viewStack->addWidget( firstPart->widget() );

    m_centralLayout = new QVBoxLayout( centralWidget );
    m_centralLayout->setSpacing( 0 );
    m_centralLayout->setMargin( 0 );
    m_centralLayout->addWidget( m_tabBar );
    m_centralLayout->addWidget( m_viewStack );
    setCentralWidget( centralWidget );

    m_tabs.append( firstPart );
    m_activeTab = 0;

    // then, setup our actions
    setupActions();
    // and integrate the part's GUI with the shell's
    setupGUI(Keys | ToolBar | Save);
    createGUI(firstPart);

    connectPart( firstPart );

    if (m_args && m_args->isSet("unique") && m_args->count() == 1)
    {
        QDBusConnection::sessionBus().registerService("org.kde.okular");
    }
    readSettings();

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

    if (m_openUrl.isValid()) QTimer::singleShot(0, this, SLOT(delayedOpen()));
  }
  else
  {
    KMessageBox::error(this, i18n("Unable to find the Okular component."));
  }
}

void Shell::delayedOpen()
{
   openUrl( m_openUrl );
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
            if( m_unique )
            {
                KMessageBox::error(this, i18n("Can't open more than one document in the unique Okular instance."));
            }
            else
            {
                Shell* newShell = new Shell();
                newShell->openUrl( url );
                newShell->show();
            }
        }
        else
        {
            openNewTab( url );
            setActiveTab( m_tabs.size()-1 );
        }
    }
    else
    {
        KParts::ReadWritePart* const emptyPart = m_tabs[m_activeTab].part;
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
  KStandardAction::quit(this, SLOT(close()), actionCollection());

  setStandardToolBarMenuEnabled(true);

  m_showMenuBarAction = KStandardAction::showMenubar( this, SLOT(slotShowMenubar()), actionCollection());
  m_fullScreenAction = KStandardAction::fullScreen( this, SLOT(slotUpdateFullScreen()), this,actionCollection() );

    m_nextTabAction = actionCollection()->addAction("tab-next");
    m_nextTabAction->setText( i18n("Next Tab") );
    m_nextTabAction->setShortcut( QKeySequence::NextChild );
    m_nextTabAction->setEnabled( false );
    connect( m_nextTabAction, SIGNAL(triggered()), this, SLOT(activateNextTab()) );

    m_prevTabAction = actionCollection()->addAction("tab-previous");
    m_prevTabAction->setText( i18n("Previous Tab") );
    m_prevTabAction->setShortcut( QKeySequence::PreviousChild );
    m_prevTabAction->setEnabled( false );
    connect( m_prevTabAction, SIGNAL(triggered()), this, SLOT(activatePrevTab()) );
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
        const KDocumentViewer* const doc = qobject_cast<KDocumentViewer*>(m_tabs[m_activeTab].part);
        if ( doc )
            m_fileformats = doc->supportedMimeTypes();

        if ( m_fileformats.isEmpty() )
            m_fileformats = fileFormats();

        m_fileformatsscanned = true;
    }

    QString startDir;
    const KParts::ReadWritePart* const curPart = m_tabs[m_activeTab].part;
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
    if( m_tabs.size() > 1 )
    {
        const int sel = KMessageBox::warningYesNoCancel(
           this,
           i18n("You have multiple tabs open in this window, are you sure you want to quit?"),
           i18n("Confimation"),
           KStandardGuiItem::quit(),
           KGuiItem(i18n("Close Current Tab"),"tab-close"),
           KStandardGuiItem::cancel(),
           i18n("CloseMultipleTabs") );

        if( sel == KMessageBox::Cancel )
            return false;

        if( sel == KMessageBox::No )
        {
           closeTab( m_activeTab );
           return false;
        }
    }

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
    m_printAction->setEnabled( m_tabs[tab].printEnabled );
    m_closeAction->setEnabled( m_tabs[tab].closeEnabled );
}

void Shell::closeTab( int tab )
{
    m_tabs[tab].part->closeUrl();
    if( m_tabs.count() > 1 )
    {
        KParts::ReadWritePart* const part = m_tabs[tab].part;
        m_viewStack->removeWidget( part->widget() );
        if( part->factory() )
            part->factory()->removeClient( part );
        part->disconnect();
        part->deleteLater();
        m_tabs.removeAt( tab );
    }

    m_tabBar->removeTab( tab );

    if( m_tabBar->count() == 1 )
    {
        m_tabBar->removeTab( 0 );
        m_nextTabAction->setEnabled( false );
        m_prevTabAction->setEnabled( false );
    }
}

void Shell::moveTab( int from, int to )
{
    m_tabs.move( from, to );
}

void Shell::openNewTab( const KUrl& url, int desiredIndex )
{
    // Tabs are hidden when there's only one, so show it
    if( m_tabs.size() == 1 )
    {
        const KUrl firstUrl = m_tabs[0].part->url();
        m_tabBar->addTab( getIcon(firstUrl), firstUrl.fileName() );
        m_nextTabAction->setEnabled( true );
        m_prevTabAction->setEnabled( true );
    }

    if( desiredIndex > m_tabs.size() )
       desiredIndex = m_tabs.size();

    const int newIndex = (desiredIndex >= 0) ? desiredIndex : m_tabs.size();

    // Make new part
    m_tabs.insert( newIndex, m_partFactory->create<KParts::ReadWritePart>(this) );
    connectPart( m_tabs[newIndex].part );

    // Update GUI
    m_viewStack->addWidget( m_tabs[newIndex].part->widget() );
    m_tabBar->insertTab( newIndex, getIcon(url), url.fileName() );

    if( m_tabs[newIndex].part->openUrl(url) )
        m_recent->addUrl( url );
}

void Shell::connectPart( QObject* part )
{
    // These signals and slots defined in Okular::Part, but since we don't
    // have access to that class, just pass in a KParts::Part* for brevity
    connect( this, SIGNAL(restoreDocument(KConfigGroup)), part, SLOT(restoreDocument(KConfigGroup)));
    connect( this, SIGNAL(saveDocumentRestoreInfo(KConfigGroup&)), part, SLOT(saveDocumentRestoreInfo(KConfigGroup&)));
    connect( part, SIGNAL(enablePrintAction(bool)), this, SLOT(setPrintEnabled(bool)));
    connect( part, SIGNAL(enableCloseAction(bool)), this, SLOT(setCloseEnabled(bool)));
}

void Shell::print()
{
    QMetaObject::invokeMethod( m_tabs[m_activeTab].part, "slotPrint" );
}

void Shell::setPrintEnabled( bool enabled )
{
    // See warnings: http://qt-project.org/doc/qt-4.8/qobject.html#sender
    const KParts::ReadWritePart* const part = qobject_cast<KParts::ReadWritePart*>(sender());
    if( !part )
       return;

    for( int i = 0; i < m_tabs.size(); ++i )
    {
       if( m_tabs[i].part == part )
       {
          m_tabs[i].printEnabled = enabled;
          if( i == m_activeTab )
             m_printAction->setEnabled( enabled );
          break;
       }
    }
}

void Shell::setCloseEnabled( bool enabled )
{
    // See warnings: http://qt-project.org/doc/qt-4.8/qobject.html#sender
    const KParts::ReadWritePart* const part = qobject_cast<KParts::ReadWritePart*>(sender());
    if( !part )
       return;

    for( int i = 0; i < m_tabs.size(); ++i )
    {
       if( m_tabs[i].part == part )
       {
          m_tabs[i].closeEnabled = enabled;
          if( i == m_activeTab )
             m_closeAction->setEnabled( enabled );
          break;
       }
    }
}

KIcon Shell::getIcon( const KUrl& url )
{
    return KIcon(KMimeType::findByUrl(url)->iconName());
}

void Shell::activateNextTab()
{
    if( m_tabs.size() < 2 )
        return;

    const int nextTab = (m_activeTab == m_tabs.size()-1) ? 0 : m_activeTab+1;

    setActiveTab( nextTab );
}

void Shell::activatePrevTab()
{
    if( m_tabs.size() < 2 )
        return;

    const int prevTab = (m_activeTab == 0) ? m_tabs.size()-1 : m_activeTab-1;

    setActiveTab( prevTab );
}

#include "shell.moc"

/* kate: replace-tabs on; indent-width 4; */
