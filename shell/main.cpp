/***************************************************************************
 *   Copyright (C) 2002 by Wilco Greven <greven@kde.org>                   *
 *   Copyright (C) 2003 by Christophe Devriese                             *
 *                         <Christophe.Devriese@student.kuleuven.ac.be>    *
 *   Copyright (C) 2003 by Laurent Montel <montel@kde.org>                 *
 *   Copyright (C) 2003-2007 by Albert Astals Cid <aacid@kde.org>          *
 *   Copyright (C) 2004 by Andy Goossens <andygoossens@telenet.be>         *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "shell.h"
#include <kapplication.h>
#include <kcmdlineargs.h>
#include <klocale.h>
#include <QtDBus/qdbusinterface.h>
#include <QTextStream>
#include "aboutdata.h"
#include "shellutils.h"
#include <iostream>

static bool attachUniqueInstance(KCmdLineArgs* args)
{
    if (!args->isSet("unique") || args->count() != 1)
        return false;

    QDBusInterface iface("org.kde.okular", "/okular1", "org.kde.okular");
    QDBusInterface iface2("org.kde.okular", "/okularshell", "org.kde.okularshell");
    if (!iface.isValid() || !iface2.isValid())
        return false;

    if (args->isSet("print"))
	iface.call("enableStartWithPrint");
    if (args->isSet("page"))
        iface.call("openDocument", ShellUtils::urlFromArg(args->arg(0), ShellUtils::qfileExistFunc(), args->getOption("page")).url());
    else
        iface.call("openDocument", ShellUtils::urlFromArg(args->arg(0), ShellUtils::qfileExistFunc()).url());
    if (args->isSet("raise")){
	iface2.call("tryRaise");
    }

    return true;
}

static bool attachExistingInstance( KCmdLineArgs* args )
{
    if( args->isSet("new") || args->count() == 0 )
        return false;

    QStringList services = QDBusConnection::sessionBus().interface()->registeredServiceNames().value();

    // Dont match the service without trailing "-" b/c that's the unique instance
    QString pattern = "org.kde.okular-";
    QString myPid = QString::number(kapp->applicationPid());
    QDBusInterface* bestService = NULL;
    QDateTime latestTime;
    latestTime.setMSecsSinceEpoch( 0 );

    foreach( const QString& service, services )
    {
        // Query all services to find the last activated instance
        if( service.startsWith(pattern) && !service.endsWith(myPid) )
        {
            QDBusInterface* iface = new QDBusInterface( service, QLatin1String("/okularshell"), QLatin1String("org.kde.okularshell") );
            QDBusReply<QDateTime> reply = iface->call( QLatin1String("lastActivationTime") );
            if( reply.isValid() )
            {
                QDateTime time = reply.value();
                if( time > latestTime )
                {
                    latestTime = time;
                    delete bestService;
                    bestService = iface;
                    iface = NULL;
                }
            }
            delete iface;
        }
    }

    if( bestService == NULL )
        return false;

    for( int i = 0; i < args->count(); ++i )
    {
        bestService->call( QLatin1String("openDocument"), args->arg(i) );
    }

    bestService->call( "tryRaise" );

    delete bestService;

    return true;
}

int main(int argc, char** argv)
{
    KAboutData about = okularAboutData( "okular", I18N_NOOP( "Okular" ) );

    KCmdLineArgs::init(argc, argv, &about);

    KCmdLineOptions options;
    options.add("p");
    options.add("page <number>", ki18n("Page of the document to be shown"));
    options.add("presentation", ki18n("Start the document in presentation mode"));
    options.add("print", ki18n("Start with print dialog"));
    options.add("unique", ki18n("\"Unique instance\" control"));
    options.add("noraise", ki18n("Not raise window"));
    options.add("new", ki18n("Force start of new instance"));
    options.add("+[URL]", ki18n("Document to open. Specify '-' to read from stdin."));
    KCmdLineArgs::addCmdLineOptions( options );
    KApplication app;

    // see if we are starting with session management
    if (app.isSessionRestored())
    {
        kRestoreMainWindows<Shell>();
    } else {
        // no session.. just start up normally
        KCmdLineArgs* args = KCmdLineArgs::parsedArgs();

        // try to attach to an existing instance, unique or otherwise
        if( attachUniqueInstance(args) || attachExistingInstance(args) )
        {
            args->clear();
            return 0;
        }

        Shell* widget = new Shell(args);
        widget->show();
    }

    return app.exec();
}

// vim:ts=2:sw=2:tw=78:et
