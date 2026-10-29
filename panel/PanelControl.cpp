// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PanelControl.h"
#include "kick_Simulator.h"

#include <typeinfo>

namespace sim
{
    namespace
    {
        void log( const juce::String& S ) { juce::Logger::writeToLog( "PanelControl: " + S ); }

        /// Depth-first search of showing components, topmost (last-added) children first.
        juce::Component* findIn( juce::Component& Root, const std::function<bool( juce::Component& )>& Match )
        {
            for( int i = Root.getNumChildComponents(); --i >= 0; )
            {
                auto* c = Root.getChildComponent( i );
                if( c == nullptr || !c->isVisible() )
                    continue;
                if( auto* hit = findIn( *c, Match ) )
                    return hit;
                if( Match( *c ) )
                    return c;
            }
            return nullptr;
        }
    } // namespace

    PanelControl::PanelControl( juce::Component& Panel, const juce::File& File, std::function<juce::String()> LinkInfo )
        : panel( Panel ), file( File ), linkInfo( std::move( LinkInfo ) )
    {
        if( !file.existsAsFile() )
            file.create();
        readPos = file.getSize(); // only lines written after start-up
        log( "listening on " + file.getFullPathName() );
        startTimer( 20 );
    }

    PanelControl::~PanelControl() { stopTimer(); }

    void PanelControl::timerCallback()
    {
        if( busy )
            return;
        if( queue.isEmpty() )
        {
            const auto size = file.getSize();
            if( size < readPos )
                readPos = 0; // the file was truncated
            if( size == readPos )
                return;
            juce::FileInputStream in( file );
            if( !in.openedOk() )
                return;
            in.setPosition( readPos );
            partial += in.readString();
            readPos = in.getPosition();
            while( partial.containsChar( '\n' ) )
            {
                queue.add( partial.upToFirstOccurrenceOf( "\n", false, false ).trim() );
                partial = partial.fromFirstOccurrenceOf( "\n", false, false );
            }
        }
        if( !queue.isEmpty() )
        {
            auto line = queue[0];
            queue.remove( 0 );
            if( line.isNotEmpty() )
                run( line );
        }
    }

    juce::Component* PanelControl::findControl( const juce::String& Name )
    {
        // "key <id>" names a key by its BtnLedSequence id. Printed labels that are not controls
        // (e.g. "MASTER" under the MASTER pot, a twin of the SHIFT + TAP TEMPO caption) do not
        // take clicks, so they are skipped.
        if( Name.startsWithIgnoreCase( "key " ) && Name.substring( 4 ).trim().containsOnly( "0123456789" ) )
        {
            const int id = Name.substring( 4 ).getIntValue();
            return findIn( panel, [id]( juce::Component& C ) {
                auto* b = dynamic_cast<SurfaceButton*>( &C );
                return b != nullptr && b->getId() == id;
            } );
        }
        return findIn( panel, [&]( juce::Component& C ) {
            bool self = false, children = false;
            C.getInterceptsMouseClicks( self, children );
            return dynamic_cast<juce::Button*>( &C ) != nullptr && self && C.getName().equalsIgnoreCase( Name );
        } );
    }

    void PanelControl::run( const juce::String& Line )
    {
        juce::StringArray tok;
        tok.addTokens( Line, " ", "\"" );
        tok.trim();
        tok.removeEmptyStrings();
        for( auto& t : tok )
            t = t.unquoted();
        if( tok.size() < 2 )
            return log( "error ? malformed line: " + Line );
        const auto seq = tok[0];
        const auto cmd = tok[1];
        tok.removeRange( 0, 2 );

        auto ok   = [seq]() { log( "ok " + seq ); };
        auto fail = [seq]( const juce::String& Why ) { log( "error " + seq + " " + Why ); };

        if( cmd == "press" )
        {
            auto* b = dynamic_cast<juce::Button*>( findControl( tok.joinIntoString( " " ) ) );
            if( b == nullptr )
                return fail( "no panel control named: " + tok.joinIntoString( " " ) );
            // triggerClick flashes the button down and up (about 100 ms), so keys and pads send
            // their press and release packets as from a click.
            b->triggerClick();
            busy = true;
            juce::Timer::callAfterDelay( 200, [this, ok] { busy = false; ok(); } );
            return;
        }

        if( cmd == "down" || cmd == "up" || cmd == "hold" )
        {
            // down NAME [VELOCITY] / up NAME / hold NAME SECONDS [VELOCITY]. NAME may contain
            // spaces ("Pad 1"): numbers at the end are the arguments.
            const int         numArgs = cmd == "down" ? 1 : ( cmd == "hold" ? 2 : 0 );
            juce::StringArray nameTok( tok ), args;
            while( args.size() < numArgs && nameTok.size() > 1 && nameTok[nameTok.size() - 1].containsOnly( "0123456789." ) )
            {
                args.insert( 0, nameTok[nameTok.size() - 1] );
                nameTok.remove( nameTok.size() - 1 );
            }
            const auto name    = nameTok.joinIntoString( " " );
            auto*      control = findControl( name );
            auto*      pad     = dynamic_cast<PadButton*>( control );
            auto*      key     = dynamic_cast<SurfaceButton*>( control );
            if( pad == nullptr && key == nullptr )
                return fail( "no pad or key named: " + name );
            if( cmd == "hold" && args.isEmpty() )
                return fail( "usage: hold NAME SECONDS [VELOCITY]" );
            const int velocity = cmd == "hold" ? ( args.size() > 1 ? args[1].getIntValue() : -1 ) : ( args.size() > 0 ? args[0].getIntValue() : -1 );
            auto      set      = [pad, key, velocity]( bool Down ) {
                if( pad != nullptr ) pad->scriptedHold( Down, velocity );
                else key->scriptedHold( Down );
            };
            if( cmd != "hold" )
            {
                set( cmd == "down" );
                return ok();
            }
            set( true );
            busy = true;
            juce::Component::SafePointer<juce::Component> safe( control );
            juce::Timer::callAfterDelay( juce::jmax( 1, (int)( args[0].getDoubleValue() * 1000.0 ) ), [this, safe, set, ok] {
                if( safe != nullptr ) set( false );
                busy = false;
                ok();
            } );
            return;
        }

        if( cmd == "turn" || cmd == "slider" )
        {
            const auto name = cmd == "slider" ? juce::String( "SLIDER" ) : tok[0].toUpperCase();
            auto*      c    = findIn( panel, [&]( juce::Component& C ) { return C.getName() == name; } );
            if( auto* knob = dynamic_cast<EndlessKnob*>( c ); knob != nullptr && cmd == "turn" && tok.size() == 2 && knob->onStep )
            {
                // One packet of magnitude 1 per detent, through the callback a mouse drag uses.
                const int steps = tok[1].getIntValue();
                for( int i = 0; i < std::abs( steps ); ++i )
                    knob->onStep( steps > 0 ? 1 : -1 );
                return ok();
            }
            if( auto* s = dynamic_cast<juce::Slider*>( c ); s != nullptr && cmd == "slider" && tok.size() == 1 )
            {
                s->setValue( juce::jlimit( 0, 100, tok[0].getIntValue() ), juce::sendNotificationSync );
                return ok();
            }
            return fail( "usage: turn P1..P4|JOG <steps> | slider <0..100>" );
        }

        if( cmd == "repeat" )
        {
            // repeat <ms> <count> <control>: on an absolute schedule (no drift), e.g. TAP TEMPO at
            // a known tempo. Acknowledged after the last press.
            const int  ms    = tok[0].getIntValue();
            const int  count = tok[1].getIntValue();
            const auto name  = juce::StringArray( tok.begin() + juce::jmin( 2, tok.size() ), juce::jmax( 0, tok.size() - 2 ) ).joinIntoString( " " );
            auto*      btn   = dynamic_cast<juce::Button*>( findControl( name ) );
            if( tok.size() < 3 || ms <= 0 || count <= 0 || btn == nullptr )
                return fail( "usage: repeat <ms> <count> <panel control> (no control '" + name + "')" );
            busy             = true;
            const auto start = juce::Time::getMillisecondCounterHiRes();
            juce::Component::SafePointer<juce::Button> safe( btn );
            auto step = std::make_shared<std::function<void( int )>>();
            *step     = [this, safe, start, ms, count, name, ok, weak = std::weak_ptr<std::function<void( int )>>( step )]( int k ) {
                if( safe != nullptr )
                    safe->triggerClick();
                log( "repeat " + name + " " + juce::String( k + 1 ) + "/" + juce::String( count ) + " at " + juce::String( juce::Time::getMillisecondCounterHiRes() - start, 1 ) + " ms" );
                if( k + 1 >= count )
                {
                    juce::Timer::callAfterDelay( 200, [this, ok] { busy = false; ok(); } );
                    return;
                }
                const auto wait = juce::roundToInt( start + ( k + 1 ) * (double)ms - juce::Time::getMillisecondCounterHiRes() );
                if( auto next = weak.lock() )
                    juce::Timer::callAfterDelay( juce::jmax( 0, wait ), [next, k] { ( *next )( k + 1 ); } );
            };
            ( *step )( 0 );
            return;
        }

        if( cmd == "screen" || cmd == "screentouch" || cmd == "screenshot" )
        {
            auto* sim    = dynamic_cast<Simulator*>( &panel );
            auto* screen = sim != nullptr ? sim->getEmbeddedScreen() : nullptr;
            if( screen == nullptr )
                return fail( "the screen is not embedded (kickem-panel --screen embedded)" );
            if( cmd == "screen" )
            {
                log( "screen " + screen->stats() );
                return ok();
            }
            if( !screen->isConnected() )
                return fail( "the embedded screen is not connected (" + screen->stats() + ")" );
            if( cmd == "screenshot" )
            {
                // The frame as received from the screen server, at its own size.
                const auto out = juce::File::getCurrentWorkingDirectory().getChildFile( tok.joinIntoString( " " ) );
                const auto img = screen->frameCopy();
                out.deleteFile();
                juce::FileOutputStream os( out );
                if( !img.isValid() || !os.openedOk() || !juce::PNGImageFormat().writeImageToStream( img, os ) )
                    return fail( "cannot write " + out.getFullPathName() );
                log( "screenshot " + out.getFullPathName() + " " + juce::String( img.getWidth() ) + "x" + juce::String( img.getHeight() ) );
                return ok();
            }
            // screentouch X Y [X2 Y2 [MS]]: a click (or a drag to X2 Y2 over MS ms) on the embedded
            // screen at screen pixels, through the same path as the mouse.
            if( !( tok.size() == 2 || tok.size() == 4 || tok.size() == 5 ) )
                return fail( "usage: screentouch X Y [X2 Y2 [MS]]" );
            const juce::Point<int> from( tok[0].getIntValue(), tok[1].getIntValue() );
            const juce::Point<int> to = tok.size() >= 4 ? juce::Point<int>( tok[2].getIntValue(), tok[3].getIntValue() ) : from;
            const int              ms = tok.size() == 5 ? tok[4].getIntValue() : 300;
            busy = true;
            screen->scriptedTouch( from, to, ms, juce::jmax( 1, ms / 20 ), [this, ok] {
                juce::Timer::callAfterDelay( 50, [this, ok] { busy = false; ok(); } );
            } );
            return;
        }

        if( cmd == "shot" )
        {
            // shot FILE.png [SCALE]: SCALE 2 renders the 1240x1250 canvas at 2480x2500.
            float scale = 1.0f;
            if( tok.size() > 1 && tok[tok.size() - 1].containsOnly( "0123456789." ) )
            {
                scale = juce::jlimit( 0.25f, 4.0f, tok[tok.size() - 1].getFloatValue() );
                tok.remove( tok.size() - 1 );
            }
            const auto out = juce::File::getCurrentWorkingDirectory().getChildFile( tok.joinIntoString( " " ) );
            const auto img = panel.createComponentSnapshot( panel.getLocalBounds(), true, scale );
            out.deleteFile();
            juce::FileOutputStream os( out );
            if( !os.openedOk() || !juce::PNGImageFormat().writeImageToStream( img, os ) )
                return fail( "cannot write " + out.getFullPathName() );
            log( "shot " + out.getFullPathName() + " " + juce::String( img.getWidth() ) + "x" + juce::String( img.getHeight() ) );
            return ok();
        }

        if( cmd == "list" )
        {
            const auto filter = tok.joinIntoString( " " );
            findIn( panel, [&]( juce::Component& C ) {
                const auto n = C.getName();
                if( n.isNotEmpty() && ( filter.isEmpty() || n.containsIgnoreCase( filter ) ) )
                    log( "item " + juce::String( typeid( C ).name() ) + "\t" + n + "\t" + C.getBounds().toString() );
                return false;
            } );
            return ok();
        }

        if( cmd == "resize" )
        {
            // resize W H: the panel's window (its content scales: ScaledPanel).
            auto* top = panel.getTopLevelComponent();
            if( tok.size() != 2 || top == nullptr )
                return fail( "usage: resize W H" );
            top->setSize( juce::jlimit( 200, 4000, tok[0].getIntValue() ), juce::jlimit( 200, 4000, tok[1].getIntValue() ) );
            const auto t = panel.getTransform();
            log( "resize window " + juce::String( top->getWidth() ) + "x" + juce::String( top->getHeight() ) + ", panel scale " + juce::String( t.mat00, 3 ) );
            return ok();
        }

        if( cmd == "link" )
        {
            log( "link " + ( linkInfo ? linkInfo() : juce::String( "?" ) ) );
            return ok();
        }

        if( cmd == "raw" || cmd == "split" || cmd == "close" || cmd == "reopen" )
        {
            if( !onLinkCommand )
                return fail( "no link" );
            const auto r = onLinkCommand( cmd, tok );
            return r.isEmpty() ? ok() : fail( r );
        }

        if( cmd == "echo" )
        {
            log( "echo " + tok.joinIntoString( " " ) );
            return ok();
        }

        fail( "unknown command " + cmd );
    }
} // namespace sim
