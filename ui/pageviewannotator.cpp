/***************************************************************************
 *   Copyright (C) 2005 by Enrico Ros <eros.kde@email.it>                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 ***************************************************************************/

#include "pageviewannotator.h"

// qt / kde includes
#include <qapplication.h>
#include <qfile.h>
#include <qcolor.h>
#include <qevent.h>
#include <qlist.h>
#include <qpainter.h>
#include <qset.h>
#include <qvariant.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kstandarddirs.h>
#include <kinputdialog.h>
#include <kuser.h>
#include <kdebug.h>
#include <kmenu.h>

// system includes
#include <math.h>

// local includes
#include "core/area.h"
#include "core/document.h"
#include "core/page.h"
#include "core/annotations.h"
#include "settings.h"
#include "annotationtools.h"
#include "pageview.h"

/** @short PickPointEngine */
class PickPointEngine : public AnnotatorEngine
{
    public:
        PickPointEngine( const QDomElement & engineElement )
            : AnnotatorEngine( engineElement ), clicked( false ), pixmap( 0 ),
              xscale( 1.0 ), yscale( 1.0 )
        {
            // parse engine specific attributes
            pixmapName = engineElement.attribute( "hoverIcon" );
            const QString stampname = m_annotElement.attribute( "icon" );
            if ( m_annotElement.attribute( "type" ) == "Stamp" && !stampname.simplified().isEmpty() )
                pixmapName = stampname;
            center = QVariant( engineElement.attribute( "center" ) ).toBool();
            bool ok = true;
            size = engineElement.attribute( "size", "32" ).toInt( &ok );
            if ( !ok )
                size = 32;
            m_block = QVariant( engineElement.attribute( "block" ) ).toBool();

            // create engine objects
            if ( !pixmapName.simplified().isEmpty() )
                pixmap = new QPixmap( DesktopIcon( pixmapName, size ) );
        }

        ~PickPointEngine()
        {
            delete pixmap;
        }

        QRect event( EventType type, Button button, double nX, double nY, double xScale, double yScale, const Okular::Page * page )
        {
            xscale=xScale;
            yscale=yScale;
            pagewidth = page->width();
            pageheight = page->height();
            // only proceed if pressing left button
            if ( button != Left )
                return QRect();

            // start operation on click
            if ( type == Press && clicked == false )
            {
                clicked = true;
                startpoint.x=nX;
                startpoint.y=nY;
            }
            // repaint if moving while pressing
            else if ( type == Move && clicked == true )
            {
            }
            // operation finished on release
            else if ( type == Release && clicked == true )
            {
                m_creationCompleted = true;
            }
            else
                return QRect();

            // update variables and extents (zoom invariant rect)
            point.x = nX;
            point.y = nY;
            if ( center && pixmap )
            {
                rect.left = nX - ( pixmap->width() / ( xScale * 2.0 ) );
                rect.top = nY - ( pixmap->height() / ( yScale * 2.0 ) );
            }
            else
            {
                rect.left = nX;
                rect.top = nY;
            }
            rect.right = rect.left + ( pixmap ? pixmap->width() / xScale : 0 );
            rect.bottom = rect.top + ( pixmap ? pixmap->height() / yScale : 0 );
            QRect boundrect = rect.geometry( (int)xScale, (int)yScale ).adjusted( 0, 0, 1, 1 );
            if ( m_block )
            {
                const Okular::NormalizedRect tmprect( qMin( startpoint.x, point.x ), qMin( startpoint.y, point.y ), qMax( startpoint.x, point.x ), qMax( startpoint.y, point.y ) );
                boundrect |= tmprect.geometry( (int)xScale, (int)yScale ).adjusted( 0, 0, 1, 1 );
            }
            return boundrect;
        }

        void paint( QPainter * painter, double xScale, double yScale, const QRect & /*clipRect*/ )
        {
            if ( clicked )
            {
                if ( m_block )
                {
                    const QPen origpen = painter->pen();
                    QPen pen = painter->pen();
                    pen.setStyle( Qt::DashLine );
                    painter->setPen( pen );
                    const Okular::NormalizedRect tmprect( qMin( startpoint.x, point.x ), qMin( startpoint.y, point.y ), qMax( startpoint.x, point.x ), qMax( startpoint.y, point.y ) );
                    const QRect realrect = tmprect.geometry( (int)xScale, (int)yScale );
                    painter->drawRect( realrect );
                    painter->setPen( origpen );
                }
                if ( pixmap )
                    painter->drawPixmap( QPointF( rect.left * xScale, rect.top * yScale ), *pixmap );
            }
        }

        QList< Okular::Annotation* > end()
        {
            m_creationCompleted = false;

            // find out annotation's description node
            if ( m_annotElement.isNull() )
                return QList< Okular::Annotation* >();

            // find out annotation's type
            Okular::Annotation * ann = 0;
            const QString typeString = m_annotElement.attribute( "type" );
            // create TextAnnotation from path
            if ( typeString == "FreeText")	//<annotation type="Text"
            {
                //note dialog
                const QString prompt = i18n( "Text of the new note:" );
                bool resok;
                const QString note = KInputDialog::getMultiLineText( i18n( "New Text Note" ), prompt, QString(), &resok );
                if(resok)
                {
                    //add note
                    Okular::TextAnnotation * ta = new Okular::TextAnnotation();
                    ann = ta;
                    ta->setContents( note );
                    ta->setTextType( Okular::TextAnnotation::InPlace );
                    //set boundary
                    rect.left = qMin(startpoint.x,point.x);
                    rect.top = qMin(startpoint.y,point.y);
                    rect.right = qMax(startpoint.x,point.x);
                    rect.bottom = qMax(startpoint.y,point.y);
                    kDebug().nospace() << "xyScale=" << xscale << "," << yscale;
                    static int padding = 2;
                    const QFontMetricsF mf(ta->textFont());
                    const QRectF rcf = mf.boundingRect( Okular::NormalizedRect( rect.left, rect.top, 1.0, 1.0 ).geometry( (int)pagewidth, (int)pageheight ).adjusted( padding, padding, -padding, -padding ),
                                                  Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, ta->contents() );
                    rect.right = qMax(rect.right, rect.left+(rcf.width()+padding*2)/pagewidth);
                    rect.bottom = qMax(rect.bottom, rect.top+(rcf.height()+padding*2)/pageheight);
                    ta->window().setSummary( i18n( "Inline Note" ) );
                }
            }
            else if ( typeString == "Text")
            {
                Okular::TextAnnotation * ta = new Okular::TextAnnotation();
                ann = ta;
                ta->setTextType( Okular::TextAnnotation::Linked );
                ta->setTextIcon( "Note" );
                //ta->window.flags &= ~(Okular::Annotation::Hidden);
                const double iconhei=0.03;
                rect.left = point.x;
                rect.top = point.y;
                rect.right=rect.left+iconhei;
                rect.bottom=rect.top+iconhei*xscale/yscale;
                ta->window().setSummary( i18n( "Note" ) );
            }
            // create StampAnnotation from path
            else if ( typeString == "Stamp" )
            {
                Okular::StampAnnotation * sa = new Okular::StampAnnotation();
                ann = sa;
                sa->setStampIconName( pixmapName );
                // set boundary
                rect.left = qMin( startpoint.x, point.x );
                rect.top = qMin( startpoint.y, point.y );
                rect.right = qMax( startpoint.x, point.x );
                rect.bottom = qMax( startpoint.y, point.y );
                const QRectF rcf = rect.geometry( (int)xscale, (int)yscale );
                const int ml = ( rcf.bottomRight() - rcf.topLeft() ).toPoint().manhattanLength();
                if ( ml <= QApplication::startDragDistance() )
                {
                    const double stampxscale = size / xscale;
                    const double stampyscale = size / yscale;
                    if ( center )
                    {
                        rect.left = point.x - stampxscale / 2;
                        rect.top = point.y - stampyscale / 2;
                    }
                    else
                    {
                        rect.left = point.x;
                        rect.top = point.y;
                    }
                    rect.right = rect.left + stampxscale;
                    rect.bottom = rect.top + stampyscale;
                }
            }
            // create GeomAnnotation
            else if ( typeString == "GeomSquare" || typeString == "GeomCircle" )
            {
                Okular::GeomAnnotation * ga = new Okular::GeomAnnotation();
                ann = ga;
                // set the type
                if ( typeString == "GeomSquare" )
                    ga->setGeometricalType( Okular::GeomAnnotation::InscribedSquare );
                else
                    ga->setGeometricalType( Okular::GeomAnnotation::InscribedCircle );
                ga->style().setWidth( 5 );
                //set boundary
                rect.left = qMin( startpoint.x, point.x );
                rect.top = qMin( startpoint.y, point.y );
                rect.right = qMax( startpoint.x, point.x );
                rect.bottom = qMax( startpoint.y, point.y );
            }

            // safety check
            if ( !ann )
                return QList< Okular::Annotation* >();

            // set common attributes
            ann->style().setColor( m_annotElement.hasAttribute( "color" ) ?
                    m_annotElement.attribute( "color" ) : m_engineColor );
            if ( m_annotElement.hasAttribute( "opacity" ) )
                ann->style().setOpacity( m_annotElement.attribute( "opacity", "1.0" ).toDouble() );

            // set the bounding rectangle, and make sure that the newly created
            // annotation lies within the page by translating it if necessary
            if ( rect.right > 1 )
            {
                rect.left -= rect.right - 1;
                rect.right = 1;
            }
            if ( rect.bottom > 1 )
            {
                rect.top -= rect.bottom - 1;
                rect.bottom = 1;
            }
            ann->setBoundingRectangle( rect );

            // return annotation
            return QList< Okular::Annotation* >() << ann;
        }

    private:
        bool clicked;
        Okular::NormalizedRect rect;
        Okular::NormalizedPoint startpoint;
        Okular::NormalizedPoint point;
        QPixmap * pixmap;
        QString pixmapName;
        int size;
        double xscale,yscale;
        double pagewidth, pageheight;
        bool center;
        bool m_block;
};

/** @short PolyLineEngine */
class PolyLineEngine : public AnnotatorEngine
{
    public:
        PolyLineEngine( const QDomElement & engineElement )
            : AnnotatorEngine( engineElement ), last( false )
        {
            // parse engine specific attributes
            m_block = engineElement.attribute( "block" ) == "true";
            bool ok = true;
            // numofpoints represents the max number of points for the current
            // polygon/polyline, with a pair of exceptions:
            // -1 means: the polyline must close on the first point (polygon)
            // 0 means: construct as many points as you want, right-click
            //   to construct the last point
            numofpoints = engineElement.attribute( "points" ).toInt( &ok );
            if ( !ok )
                numofpoints = -1;
        }

        QRect event( EventType type, Button button, double nX, double nY, double xScale, double yScale, const Okular::Page * /*page*/ )
        {
            // only proceed if pressing left button
//            if ( button != Left )
//                return rect;

            // start operation
            if ( type == Press )
            {
                newPoint.x = nX;
                newPoint.y = nY;
                if ( button == Right )
                    last = true;
            }
            // move the second point
            else if ( type == Move )
            {
                movingpoint.x = nX;
                movingpoint.y = nY;
                const QRect oldmovingrect = movingrect;
                movingrect = rect | QRect( (int)( movingpoint.x * xScale ), (int)( movingpoint.y * yScale ), 1, 1 );
                return oldmovingrect | movingrect;
            }
            else if ( type == Release )
            {
                const Okular::NormalizedPoint tmppoint(nX, nY);
                if ( fabs( tmppoint.x - newPoint.x + tmppoint.y - newPoint.y ) > 1e-2 )
                    return rect;

                if ( numofpoints == -1 && points.count() > 1 && ( fabs( points[0].x - newPoint.x + points[0].y - newPoint.y ) < 1e-2 ) )
                {
                    last = true;
                }
                else
                {
                    points.append( newPoint );
                    rect |= QRect( (int)( newPoint.x * xScale ), (int)( newPoint.y * yScale ), 1, 1 );
                }
                // end creation if we have constructed the last point of enough points
                if ( last || points.count() == numofpoints )
                {
                    m_creationCompleted = true;
                    last = false;
                    normRect = Okular::NormalizedRect( rect, xScale, yScale );
                }
            }

            return rect;
        }

        void paint( QPainter * painter, double xScale, double yScale, const QRect & /*clipRect*/ )
        {
            if ( points.count() < 1 )
                return;

            if ( m_block && points.count() == 2 )
            {
                const Okular::NormalizedPoint first = points[0];
                const Okular::NormalizedPoint second = points[1];
                // draw a semitransparent block around the 2 points
                painter->setPen( m_engineColor );
                painter->setBrush( QBrush( m_engineColor.light(), Qt::Dense4Pattern ) );
                painter->drawRect( (int)(first.x * (double)xScale), (int)(first.y * (double)yScale),
                                   (int)((second.x - first.x) * (double)xScale), (int)((second.y - first.y) * (double)yScale) );
            }
            else
            {
                // draw a polyline that connects the constructed points
                painter->setPen( QPen( m_engineColor, 2 ) );
                for ( int i = 1; i < points.count(); ++i )
                    painter->drawLine( (int)(points[i - 1].x * (double)xScale), (int)(points[i - 1].y * (double)yScale),
                                       (int)(points[i].x * (double)xScale), (int)(points[i].y * (double)yScale) );
                painter->drawLine( (int)(points.last().x * (double)xScale), (int)(points.last().y * (double)yScale),
                                   (int)(movingpoint.x * (double)xScale), (int)(movingpoint.y * (double)yScale) );
            }
        }

        QList< Okular::Annotation* > end()
        {
            m_creationCompleted = false;

            // find out annotation's description node
            if ( m_annotElement.isNull() )
                return QList< Okular::Annotation* >();

            // find out annotation's type
            Okular::Annotation * ann = 0;
            const QString typeString = m_annotElement.attribute( "type" );

            // create LineAnnotation from path
            if ( typeString == "Line" || typeString == "Polyline" || typeString == "Polygon" )
            {
                if ( points.count() < 2 )
                    return QList< Okular::Annotation* >();

                //add note
                Okular::LineAnnotation * la = new Okular::LineAnnotation();
                ann = la;
                QLinkedList<Okular::NormalizedPoint> list;
                for ( int i = 0; i < points.count(); ++i )
                    list.append( points[ i ] );

                la->setLinePoints( list );

                if ( numofpoints == -1 )
                    la->setLineClosed( true );

                la->setBoundingRectangle( normRect );

            }

            // safety check
            if ( !ann )
                return QList< Okular::Annotation* >();

            // set common attributes
            ann->style().setColor( m_annotElement.hasAttribute( "color" ) ?
                    m_annotElement.attribute( "color" ) : m_engineColor );
            if ( m_annotElement.hasAttribute( "opacity" ) )
                ann->style().setOpacity( m_annotElement.attribute( "opacity", "1.0" ).toDouble() );
            // return annotation

            return QList< Okular::Annotation* >() << ann;
        }

    private:
        QList<Okular::NormalizedPoint> points;
        Okular::NormalizedPoint newPoint;
        Okular::NormalizedPoint movingpoint;
        QRect rect;
        QRect movingrect;
        Okular::NormalizedRect normRect;
        bool m_block;
        bool last;
        int numofpoints;
};

/** @short TextSelectorEngine */
class TextSelectorEngine : public AnnotatorEngine
{
    public:
        TextSelectorEngine( const QDomElement & engineElement, PageView * pageView )
            : AnnotatorEngine( engineElement ), m_pageView( pageView ),
            selection( 0 )
        {
            // parse engine specific attributes
        }

        ~TextSelectorEngine()
        {
            delete selection;
        }

        QRect event( EventType type, Button button, double nX, double nY, double xScale, double yScale, const Okular::Page * /*page*/ )
        {
            // only proceed if pressing left button
            if ( button != Left )
                return QRect();

            if ( type == Press )
            {
                lastPoint.x = nX;
                lastPoint.y = nY;
                const QRect oldrect = rect;
                rect = QRect();
                return oldrect;
            }
            else if ( type == Move )
            {
                if ( item() )
                {
                    const QPoint start( (int)( lastPoint.x * item()->uncroppedWidth() ), (int)( lastPoint.y * item()->uncroppedHeight() ) );
                    const QPoint end( (int)( nX * item()->uncroppedWidth() ), (int)( nY * item()->uncroppedHeight() ) );
                    delete selection;
                    selection = 0;
                    Okular::RegularAreaRect * newselection = m_pageView->textSelectionForItem( item(), start, end );
                    if ( !newselection->isEmpty() )
                    {
                        const QList<QRect> geom = newselection->geometry( (int)xScale, (int)yScale );
                        QRect newrect;
                        Q_FOREACH ( const QRect& r, geom )
                        {
                            if ( newrect.isNull() )
                                newrect = r;
                            else
                                newrect |= r;
                        }
                        rect |= newrect;
                        selection = newselection;
                    }
                    else
                    {
                        delete newselection;
                    }
                }
            }
            else if ( type == Release && selection )
            {
                m_creationCompleted = true;
            }
            return rect;
        }

        void paint( QPainter * painter, double xScale, double yScale, const QRect & /*clipRect*/ )
        {
            if ( selection )
            {
                painter->setPen( Qt::NoPen );
                QColor col = m_engineColor;
                col.setAlphaF( 0.5 );
                painter->setBrush( col );
                foreach( const Okular::NormalizedRect & r, *selection )
                {
                    painter->drawRect( r.geometry( (int)xScale, (int)yScale ) );
                }
            }
        }

        QList< Okular::Annotation* > end()
        {
            m_creationCompleted = false;

            // safety checks
            if ( m_annotElement.isNull() || !selection )
                return QList< Okular::Annotation* >();

            // find out annotation's type
            Okular::Annotation * ann = 0;
            const QString typeString = m_annotElement.attribute( "type" );

            Okular::HighlightAnnotation::HighlightType type = Okular::HighlightAnnotation::Highlight;
            bool typevalid = false;
            // create HighlightAnnotation's from the selected area
            if ( typeString == "Highlight" )
            {
                type = Okular::HighlightAnnotation::Highlight;
                typevalid = true;
            }
            else if ( typeString == "Squiggly" )
            {
                type = Okular::HighlightAnnotation::Squiggly;
                typevalid = true;
            }
            else if ( typeString == "Underline" )
            {
                type = Okular::HighlightAnnotation::Underline;
                typevalid = true;
            }
            else if ( typeString == "StrikeOut" )
            {
                type = Okular::HighlightAnnotation::StrikeOut;
                typevalid = true;
            }
            if ( typevalid )
            {
                Okular::HighlightAnnotation * ha = new Okular::HighlightAnnotation();
                ha->setHighlightType( type );
                ha->setBoundingRectangle( Okular::NormalizedRect( rect, item()->uncroppedWidth(), item()->uncroppedHeight() ) );
                foreach ( const Okular::NormalizedRect & r, *selection )
                {
                    Okular::HighlightAnnotation::Quad q;
                    q.setCapStart( false );
                    q.setCapEnd( false );
                    q.setFeather( 1.0 );
                    q.setPoint( Okular::NormalizedPoint( r.left, r.bottom ), 0 );
                    q.setPoint( Okular::NormalizedPoint( r.right, r.bottom ), 1 );
                    q.setPoint( Okular::NormalizedPoint( r.right, r.top ), 2 );
                    q.setPoint( Okular::NormalizedPoint( r.left, r.top ), 3 );
                    ha->highlightQuads().append( q );
                }
                ann = ha;
            }

            delete selection;
            selection = 0;

            // safety check
            if ( !ann )
                return QList< Okular::Annotation* >();

            // set common attributes
            ann->style().setColor( m_annotElement.hasAttribute( "color" ) ?
                m_annotElement.attribute( "color" ) : m_engineColor );
            if ( m_annotElement.hasAttribute( "opacity" ) )
                ann->style().setOpacity( m_annotElement.attribute( "opacity", "1.0" ).toDouble() );

            // return annotations
            return QList< Okular::Annotation* >() << ann;
        }

    private:
        // data
        PageView * m_pageView;
        // TODO: support more pages
        Okular::RegularAreaRect * selection;
        Okular::NormalizedPoint lastPoint;
        QRect rect;
};


/** PageViewAnnotator **/

PageViewAnnotator::PageViewAnnotator( PageView * parent, Okular::Document * storage )
    : QObject( parent ), m_document( storage ), m_pageView( parent ),
    m_toolBar( 0 ), m_engine( 0 ), m_textToolsEnabled( false ), m_toolsEnabled( false ),
    m_continuousMode( false ), m_hidingWasForced( false ), m_lastToolID( -1 ), m_lockedItem( 0 )
{
    // load the tools from the 'xml tools definition' file. store the tree internally.
    QFile infoFile( KStandardDirs::locate("data", "okular/tools.xml") );
    if ( infoFile.exists() && infoFile.open( QIODevice::ReadOnly ) )
    {
        QDomDocument doc( "annotatingTools" );
        if ( doc.setContent( &infoFile ) )
        {
            m_toolsDefinition = doc.elementsByTagName("annotatingTools").item( 0 ).toElement();

            // create the AnnotationToolItems from the XML dom tree
            QDomNode toolDescription = m_toolsDefinition.firstChild();
            while ( toolDescription.isElement() )
            {
                QDomElement toolElement = toolDescription.toElement();
                if ( toolElement.tagName() == "tool" )
                {
                    AnnotationToolItem item;
                    item.id = toolElement.attribute("id").toInt();
                    item.text = i18n( toolElement.attribute( "name" ).toUtf8() );
                    item.pixmap = toolElement.attribute("pixmap");
                    QDomNode shortcutNode = toolElement.elementsByTagName( "shortcut" ).item( 0 );
                    if ( shortcutNode.isElement() )
                        item.shortcut = shortcutNode.toElement().text();
                    QDomNodeList engineNodeList = toolElement.elementsByTagName( "engine" );
                    if ( engineNodeList.size() > 0 )
                    {
                        QDomElement engineEl = engineNodeList.item( 0 ).toElement();
                        if ( !engineEl.isNull() && engineEl.hasAttribute( "type" ) )
                            item.isText = engineEl.attribute( "type" ) == QLatin1String( "TextSelector" );
                    }
                    m_items.push_back( item );
                }
                toolDescription = toolDescription.nextSibling();
            }
        }
        else
            kWarning() << "AnnotatingTools XML file seems to be damaged";
        infoFile.close();
    }
    else
        kWarning() << "Unable to open AnnotatingTools XML definition";
}

PageViewAnnotator::~PageViewAnnotator()
{
    delete m_engine;
}

void PageViewAnnotator::setEnabled( bool on )
{
    if ( !on )
    {
        // remove toolBar
        if ( m_toolBar )
            m_toolBar->hideAndDestroy();
        m_toolBar = 0;
        // deactivate the active tool, if any
        slotToolSelected( -1 );
        return;
    }

    // if no tools are defined, don't show the toolbar
    if ( !m_toolsDefinition.hasChildNodes() )
        return;

    // create toolBar
    if ( !m_toolBar )
    {
        m_toolBar = new PageViewToolBar( m_pageView, m_pageView->viewport() );
        m_toolBar->setSide( (PageViewToolBar::Side)Okular::Settings::editToolBarPlacement() );
        m_toolBar->setItems( m_items );
        m_toolBar->setToolsEnabled( m_toolsEnabled );
        m_toolBar->setTextToolsEnabled( m_textToolsEnabled );
        connect( m_toolBar, SIGNAL(toolSelected(int)),
                this, SLOT(slotToolSelected(int)) );
        connect( m_toolBar, SIGNAL(orientationChanged(int)),
                this, SLOT(slotSaveToolbarOrientation(int)) );
        
        connect( m_toolBar, SIGNAL(buttonDoubleClicked(int)),
                this, SLOT(slotToolDoubleClicked(int)) );
    }

    // show the toolBar
    m_toolBar->showAndAnimate();
}

void PageViewAnnotator::setTextToolsEnabled( bool enabled )
{
    m_textToolsEnabled = enabled;
    if ( m_toolBar )
        m_toolBar->setTextToolsEnabled( m_textToolsEnabled );
}

void PageViewAnnotator::setToolsEnabled( bool enabled )
{
    m_toolsEnabled = enabled;
    if ( m_toolBar )
        m_toolBar->setToolsEnabled( m_toolsEnabled );
}

void PageViewAnnotator::setHidingForced( bool forced )
{
    m_hidingWasForced = forced;
}

bool PageViewAnnotator::hidingWasForced() const
{
    return m_hidingWasForced;
}

bool PageViewAnnotator::routeEvents() const
{
    return m_engine && m_toolBar;
}

QRect PageViewAnnotator::performRouteMouseOrTabletEvent(const AnnotatorEngine::EventType & eventType, const AnnotatorEngine::Button & button,
                                                        const QPointF & pos, PageViewItem * item )
{
    // if the right mouse button was pressed, we simply do nothing. In this way, we are still editing the annotation
    // and so this function will receive and process the right mouse button release event too. If we detach now the annotation tool,
    // the release event will be processed by the PageView class which would create the annotation property widget, and we do not want this.
    if ( button == AnnotatorEngine::Right && eventType == AnnotatorEngine::Press )
        return QRect();
    else if ( button == AnnotatorEngine::Right && eventType == AnnotatorEngine::Release )
    {
        detachAnnotation();
        return QRect();
    }

    // 1. lock engine to current item
    if ( !m_lockedItem && eventType == AnnotatorEngine::Press )
    {
        m_lockedItem = item;
        m_engine->setItem( m_lockedItem );
    }
    if ( !m_lockedItem ) {
        return QRect();
    }

    // find out normalized mouse coords inside current item
    const QRect & itemRect = m_lockedItem->uncroppedGeometry();
    const QPointF eventPos = m_pageView->contentAreaPoint( pos );
    const double nX = qBound( 0.0, m_lockedItem->absToPageX( eventPos.x() ), 1.0 );
    const double nY = qBound( 0.0, m_lockedItem->absToPageY( eventPos.y() ), 1.0 );

    QRect modifiedRect;

    // 2. use engine to perform operations
    const QRect paintRect = m_engine->event( eventType, button, nX, nY, itemRect.width(), itemRect.height(), m_lockedItem->page() );

    // 3. update absolute extents rect and send paint event(s)
    if ( paintRect.isValid() )
    {
        // 3.1. unite old and new painting regions
        QRegion compoundRegion( m_lastDrawnRect );
        m_lastDrawnRect = paintRect;
        m_lastDrawnRect.translate( itemRect.left(), itemRect.top() );
        // 3.2. decompose paint region in rects and send paint events
        const QVector<QRect> rects = compoundRegion.unite( m_lastDrawnRect ).rects();
        const QPoint areaPos = m_pageView->contentAreaPosition();
        for ( int i = 0; i < rects.count(); i++ )
            m_pageView->viewport()->update( rects[i].translated( -areaPos ) );
        modifiedRect = compoundRegion.boundingRect() | m_lastDrawnRect;
    }

    // 4. if engine has finished, apply Annotation to the page
    if ( m_engine->creationCompleted() )
    {
        // apply engine data to the Annotation's and reset engine
        QList< Okular::Annotation* > annotations = m_engine->end();
        // attach the newly filled annotations to the page
        foreach ( Okular::Annotation * annotation, annotations )
        {
            if ( !annotation ) continue;

            annotation->setCreationDate( QDateTime::currentDateTime() );
            annotation->setModificationDate( QDateTime::currentDateTime() );
            annotation->setAuthor( Okular::Settings::identityAuthor() );
            m_document->addPageAnnotation( m_lockedItem->pageNumber(), annotation );
            
            if ( annotation->openDialogAfterCreation() )
                m_pageView->openAnnotationWindow( annotation, m_lockedItem->pageNumber() );
        }

        if ( m_continuousMode )
            slotToolSelected( m_lastToolID );
        else
            detachAnnotation();
    }

    return modifiedRect;
}

QRect PageViewAnnotator::routeMouseEvent( QMouseEvent * e, PageViewItem * item )
{
    AnnotatorEngine::EventType eventType;
    AnnotatorEngine::Button button;

    // figure out the event type and button
    AnnotatorEngine::decodeEvent( e, &eventType, &button );

    return performRouteMouseOrTabletEvent( eventType, button, e->posF(), item );
}

QRect PageViewAnnotator::routeTabletEvent( QTabletEvent * e, PageViewItem * item, const QPoint & localOriginInGlobal )
{
    // Unlike routeMouseEvent, routeTabletEvent must explicitly ignore events it doesn't care about so that
    // the corresponding mouse event will later be delivered.
    if ( !item )
    {
        e->ignore();
        return QRect();
    }

    // We set all tablet events that take place over the annotations toolbar to ignore so that corresponding mouse
    // events will be delivered to the toolbar.  However, we still allow the annotations code to handle
    // TabletMove and TabletRelease events in case the user is drawing an annotation onto the toolbar.
    const QPoint toolBarPos = m_toolBar->mapFromGlobal( e->globalPos() );
    const QRect toolBarRect = m_toolBar->rect();
    if ( toolBarRect.contains( toolBarPos ) )
    {
        e->ignore();
        if (e->type() == QEvent::TabletPress)
            return QRect();
    }

    AnnotatorEngine::EventType eventType;
    AnnotatorEngine::Button button;

    // figure out the event type and button
    AnnotatorEngine::decodeEvent( e, &eventType, &button );

    const QPointF globalPosF = e->hiResGlobalPos();
    const QPointF localPosF = globalPosF - localOriginInGlobal;
    return performRouteMouseOrTabletEvent( eventType, button, localPosF, item );
}

bool PageViewAnnotator::routeKeyEvent( QKeyEvent * event )
{
    if ( event->key() == Qt::Key_Escape )
    {
        detachAnnotation();
        return true;
    }
    return false;
}

bool PageViewAnnotator::routePaints( const QRect & wantedRect ) const
{
    return m_engine && m_toolBar && wantedRect.intersects( m_lastDrawnRect ) && m_lockedItem;
}

void PageViewAnnotator::routePaint( QPainter * painter, const QRect & paintRect )
{
    // if there's no locked item, then there's no decided place to draw on
    if ( !m_lockedItem )
        return;

#ifndef NDEBUG
    // [DEBUG] draw the paint region if enabled
    if ( Okular::Settings::debugDrawAnnotationRect() )
        painter->drawRect( paintRect );
#endif
    // move painter to current itemGeometry rect
    const QRect & itemRect = m_lockedItem->uncroppedGeometry();
    painter->save();
    painter->translate( itemRect.topLeft() );
    // TODO: Clip annotation painting to cropped page.

    // transform cliprect from absolute to item relative coords
    QRect annotRect = paintRect.intersect( m_lastDrawnRect );
    annotRect.translate( -itemRect.topLeft() );

    // use current engine for painting (in virtual page coordinates)
    m_engine->paint( painter, m_lockedItem->uncroppedWidth(), m_lockedItem->uncroppedHeight(), annotRect );
    painter->restore();
}

void PageViewAnnotator::slotToolSelected( int toolID )
{
    // terminate any previous operation
    if ( m_engine )
    {
        delete m_engine;
        m_engine = 0;
    }
    m_lockedItem = 0;
    if ( m_lastDrawnRect.isValid() )
    {
        m_pageView->viewport()->update( m_lastDrawnRect.translated( -m_pageView->contentAreaPosition() ) );
        m_lastDrawnRect = QRect();
    }

    if ( toolID != m_lastToolID ) m_continuousMode = false;
    // store current tool for later usage
    m_lastToolID = toolID;

    // handle tool deselection
    if ( toolID == -1 )
    {
        m_pageView->displayMessage( QString() );
        return;
    }

    // for the selected tool create the Engine
    QDomNode toolNode = m_toolsDefinition.firstChild();
    while ( toolNode.isElement() )
    {
        QDomElement toolElement = toolNode.toElement();
        toolNode = toolNode.nextSibling();

        // only find out the element describing selected tool
        if ( toolElement.tagName() != "tool" || toolElement.attribute("id").toInt() != toolID )
            continue;

        // parse tool properties
        QDomNode toolSubNode = toolElement.firstChild();
        while ( toolSubNode.isElement() )
        {
            QDomElement toolSubElement = toolSubNode.toElement();
            toolSubNode = toolSubNode.nextSibling();

            // create the AnnotatorEngine
            if ( toolSubElement.tagName() == "engine" )
            {
                QString type = toolSubElement.attribute( "type" );
                if ( type == "SmoothLine" )
                    m_engine = new SmoothPathEngine( toolSubElement );
                else if ( type == "PickPoint" )
                    m_engine = new PickPointEngine( toolSubElement );
                else if ( type == "PolyLine" )
                    m_engine = new PolyLineEngine( toolSubElement );
                else if ( type == "TextSelector" )
                    m_engine = new TextSelectorEngine( toolSubElement, m_pageView );
                else
                    kWarning().nospace() << "tools.xml: engine type:'" << type << "' is not defined!";
            }
            // display the tooltip
            else if ( toolSubElement.tagName() == "tooltip" )
            {
                const QString tip = toolSubElement.text();
                if ( !tip.isEmpty() && !m_continuousMode )
                    m_pageView->displayMessage( i18nc( "Annotation tool", tip.toUtf8() ), QString(), PageViewMessage::Annotation );
            }
        }

        // consistancy warning
        if ( !m_engine )
        {
            kWarning() << "tools.xml: couldn't find good engine description. check xml.";
        }

        // stop after parsing selected tool's node
        break;
    }
}

void PageViewAnnotator::slotSaveToolbarOrientation( int side )
{
    Okular::Settings::setEditToolBarPlacement( (int)side );
    Okular::Settings::self()->writeConfig();
}

void PageViewAnnotator::slotToolDoubleClicked( int /*toolID*/ )
{
    m_continuousMode = true;
}

void PageViewAnnotator::detachAnnotation()
{
    m_toolBar->selectButton( -1 );
}

#include "pageviewannotator.moc"

/* kate: replace-tabs on; indent-width 4; */
