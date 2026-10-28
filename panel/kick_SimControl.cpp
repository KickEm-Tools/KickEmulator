/*
 * Copyright (c) 2024 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "kick_SimControl.h"
#include "kick_Simulator.h"

#include <typeinfo>

namespace sim
{
    namespace
    {
        void log( const juce::String& S ) { juce::Logger::writeToLog( "SimControl: " + S ); }

        bool isInside( const juce::Component* C, const juce::Component* Ancestor )
        {
            for( ; C != nullptr; C = C->getParentComponent() )
                if( C == Ancestor )
                    return true;
            return false;
        }

        /// Depth-first search of showing components, topmost (last-added) children first.
        juce::Component* findIn( juce::Component& Root, const std::function<bool( juce::Component& )>& Match )
        {
            for( int i = Root.getNumChildComponents(); --i >= 0; )
            {
                auto* c = Root.getChildComponent( i );
                if( c == nullptr || !c->isShowing() )
                    continue;
                if( auto* hit = findIn( *c, Match ) )
                    return hit;
                if( Match( *c ) )
                    return c;
            }
            return nullptr;
        }

        juce::String nameOf( const juce::Component& C )
        {
            return C.getTitle().isNotEmpty() ? C.getTitle() : C.getName();
        }

        /// The app's windows, the panel window first.
        juce::Array<juce::Component*> windows( juce::Component& Panel )
        {
            juce::Array<juce::Component*> list;
            auto*                         main = Panel.getTopLevelComponent();
            list.add( main );
            auto& desk = juce::Desktop::getInstance();
            for( int i = 0; i < desk.getNumComponents(); ++i )
                if( auto* c = desk.getComponent( i ); c != main && c->isShowing() )
                    list.add( c );
            return list;
        }
    }

    SimControl::SimControl( juce::Component& Panel, juce::Component& Screen )
        : panel( Panel ), screen( Screen )
    {
        auto path = juce::SystemStats::getEnvironmentVariable( "KICK_SIM_CONTROL", {} );
        if( path.isEmpty() )
            return;
        file    = juce::File::getCurrentWorkingDirectory().getChildFile( path );
        readPos = file.getSize(); // only lines written after start-up
        log( "listening on " + file.getFullPathName() );
        startTimer( 50 );
    }

    SimControl::~SimControl() { stopTimer(); }

    void SimControl::timerCallback()
    {
        if( busy )
            return;

        if( queue.isEmpty() )
        {
            const auto size = file.getSize();
            if( size < readPos )
                readPos = 0; // file was truncated
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

    void SimControl::run( const juce::String& Line )
    {
        juce::StringArray tok;
        tok.addTokens( Line, " ", "\"" );
        tok.trim();
        tok.removeEmptyStrings();
        for( auto& t : tok )
            t = t.unquoted();
        if( tok.size() < 2 )
        {
            log( "error ? malformed line: " + Line );
            return;
        }
        const auto seq = tok[0];
        const auto cmd = tok[1];
        tok.removeRange( 0, 2 );

        auto ok   = [seq]() { log( "ok " + seq ); };
        auto fail = [seq]( const juce::String& Why ) { log( "error " + seq + " " + Why ); };

        if( cmd == "press" || cmd == "click" || cmd == "tap" || cmd == "clickat" )
        {
            juce::Component*   target = nullptr;
            juce::Point<float> pos;
            if( cmd == "press" )
                target = findPanelControl( tok.joinIntoString( " " ) );
            else if( cmd == "click" )
                target = findScreenComponent( tok.joinIntoString( " " ) );
            else if( cmd == "clickat" && tok.size() >= 3 )
            {
                // clickat <title> <fx> <fy>: a touch at that fraction of the component's size, for
                // controls with untitled parts (the On half of an Off / On toggle).
                const auto fx = tok[tok.size() - 2].getFloatValue(), fy = tok[tok.size() - 1].getFloatValue();
                target        = findScreenComponent( juce::StringArray( tok.begin(), tok.size() - 2 ).joinIntoString( " " ) );
                if( target != nullptr )
                {
                    pos = { fx * (float)target->getWidth(), fy * (float)target->getHeight() };
                    auto r = touch( *target, pos, ok );
                    if( r.isNotEmpty() )
                        fail( r );
                    return;
                }
            }
            else if( tok.size() == 2 )
            {
                target = &screen;
                pos    = { tok[0].getFloatValue(), tok[1].getFloatValue() };
            }
            if( target == nullptr )
                return fail( "no showing component for: " + tok.joinIntoString( " " ) );
            if( cmd != "tap" )
                pos = target->getLocalBounds().toFloat().getCentre();
            auto r = touch( *target, pos, ok );
            if( r.isNotEmpty() )
                fail( r );
            return;
        }

        if( cmd == "down" || cmd == "up" || cmd == "hold" )
        {
            // down NAME [VELOCITY] / up NAME: press or release a pad or key and leave it so, e.g.
            // ERASE or NOTE REPEAT held while pads are hit. hold NAME SECONDS [VELOCITY]: down,
            // wait, up; answers after the release. NAME may contain spaces ("Pad 1"): numbers at
            // the end are the arguments.
            const int numArgs = cmd == "down" ? 1 : ( cmd == "hold" ? 2 : 0 );
            juce::StringArray nameTok( tok );
            juce::StringArray args;
            while( args.size() < numArgs && nameTok.size() > 1 && nameTok[nameTok.size() - 1].containsOnly( "0123456789." ) )
            {
                args.insert( 0, nameTok[nameTok.size() - 1] );
                nameTok.remove( nameTok.size() - 1 );
            }
            const auto name    = nameTok.joinIntoString( " " );
            auto*      control = findPanelControl( name );
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
            juce::Timer::callAfterDelay( juce::jmax( 1, (int)( args[0].getDoubleValue() * 1000.0 ) ), [this, safe, set, ok]() {
                if( safe != nullptr ) set( false );
                busy = false;
                ok();
            } );
            return;
        }

        if( cmd == "shot" )
        {
            auto r = snapshot( juce::File::getCurrentWorkingDirectory().getChildFile( tok.joinIntoString( " " ) ) );
            return r.isEmpty() ? ok() : fail( r );
        }

        if( cmd == "windows" )
        {
            // Every showing desktop window, and every showing child laid over the screen (the
            // app's popups, dialogs and notifications), with its bounds on the Mac's display.
            auto wins = windows( panel );
            for( int i = 0; i < wins.size(); ++i )
                log( "window " + juce::String( i == 0 ? "main" : "desktop" ) + "\t" + typeid( *wins[i] ).name() + "\t" + wins[i]->getScreenBounds().toString() );
            for( auto* c : screen.getChildren() )
                if( c->isVisible() && ( c->isCurrentlyModal( false ) || dynamic_cast<juce::BubbleComponent*>( c ) != nullptr ) )
                    log( "window overlay\t" + juce::String( typeid( *c ).name() ) + "\t" + c->getScreenBounds().toString() );
            log( "window screen\t" + juce::String( typeid( screen ).name() ) + "\t" + screen.getScreenBounds().toString() );
            return ok();
        }

        if( cmd == "list" )
        {
            const auto filter = tok.joinIntoString( " " );
            for( auto* w : windows( panel ) )
                findIn( *w, [&]( juce::Component& C ) {
                    const auto n = nameOf( C );
                    if( n.isNotEmpty() && ( filter.isEmpty() || n.containsIgnoreCase( filter ) ) )
                        log( "item " + juce::String( typeid( C ).name() ) + "\t" + n + "\t" + C.getScreenBounds().toString()
                             + ( isInside( &C, &screen ) ? "\tscreen" : ( w == panel.getTopLevelComponent() ? "\tpanel" : "\tpopup" ) ) );
                    return false;
                } );
            return ok();
        }

        if( cmd == "turn" || cmd == "slider" )
        {
            // Encoders: one EN_PKT_TYPE_KNOBS packet of magnitude 1 per detent, through the same
            // onStep callback a mouse drag uses. Slider: one absolute 0..100 value.
            const auto name = cmd == "slider" ? juce::String( "SLIDER" ) : tok[0].toUpperCase();
            auto*      c    = findIn( *panel.getTopLevelComponent(), [&]( juce::Component& C ) { return C.getName() == name; } );
            if( auto* knob = dynamic_cast<EndlessKnob*>( c ); knob != nullptr && cmd == "turn" && tok.size() == 2 && knob->onStep )
            {
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
            // repeat <ms> <count> <control>: presses a panel control <count> times, <ms> apart, on
            // an absolute schedule (no drift), e.g. TAP TEMPO at a known tempo. Acknowledged after
            // the last press; each press is logged with its time.
            const int  ms    = tok[0].getIntValue();
            const int  count = tok[1].getIntValue();
            const auto name  = juce::StringArray( tok.begin() + juce::jmin( 2, tok.size() ), juce::jmax( 0, tok.size() - 2 ) ).joinIntoString( " " );
            auto*      btn   = dynamic_cast<juce::Button*>( findPanelControl( name ) );
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
                    juce::Timer::callAfterDelay( 200, [this, ok]() { busy = false; ok(); } );
                    return;
                }
                const auto wait = juce::roundToInt( start + ( k + 1 ) * (double)ms - juce::Time::getMillisecondCounterHiRes() );
                if( auto next = weak.lock() )
                    juce::Timer::callAfterDelay( juce::jmax( 0, wait ), [next, k]() { ( *next )( k + 1 ); } );
            };
            ( *step )( 0 );
            return;
        }

        if( cmd == "status" )
        {
            log( "status " + ( statusProvider ? statusProvider() : juce::String( "?" ) ) );
            return ok();
        }

        if( cmd == "echo" )
        {
            log( "echo " + tok.joinIntoString( " " ) );
            return ok();
        }

        fail( "unknown command " + cmd );
    }

    juce::Component* SimControl::findPanelControl( const juce::String& Name )
    {
        // Printed labels that are not controls (e.g. "MASTER" under the MASTER pot, a twin of the
        // SHIFT + TAP TEMPO caption) do not take clicks: skip them so the control is found.
        return findIn( *panel.getTopLevelComponent(), [&]( juce::Component& C ) {
            bool self = false, children = false;
            C.getInterceptsMouseClicks( self, children );
            return dynamic_cast<juce::Button*>( &C ) != nullptr && self && C.getName() == Name && !isInside( &C, &screen );
        } );
    }

    juce::Component* SimControl::findScreenComponent( const juce::String& Name )
    {
        // Zero-size components are skipped: some dialog contents carry unused 0x0 "Apply" /
        // "Cancel" buttons of the same name as the dialog's real ones (AppSelectProgramMode).
        auto match = [&]( juce::Component& C ) { return nameOf( C ) == Name && !C.getLocalBounds().isEmpty(); };
        // Popups sit above the screen, so look there first.
        auto wins = windows( panel );
        for( int i = wins.size(); --i >= 1; )
            if( auto* hit = findIn( *wins[i], match ) )
                return hit;
        if( auto* hit = findIn( screen, match ) )
            return hit;
        // The rest of the window, which holds the panel's own buttons (and any popup not yet
        // parented to the screen, see ui::simScreen()).
        return findIn( *panel.getTopLevelComponent(), match );
    }

    juce::String SimControl::touch( juce::Component& Target, juce::Point<float> Pos, std::function<void()> Done )
    {
        // Events are delivered to the component directly rather than through the window's
        // ComponentPeer: JUCE's peer dispatch asks macOS whether the window is the topmost one
        // at that point, which is false while the screen is locked or the window is covered.
        // Hit-test from the window, like a finger: whatever is on top at that point (an overlay,
        // a dialog) gets the touch, not necessarily the component that was named.
        auto* top = Target.getTopLevelComponent();
        auto* c   = top->getComponentAt( top->getLocalPoint( &Target, Pos ).roundToInt() );
        if( c == nullptr )
            c = &Target;
        const auto touchedPos = c->getLocalPoint( &Target, Pos );
        log( "touched " + nameOf( *c ) + " (" + typeid( *c ).name() + ")" );
        if( c->isCurrentlyBlockedByAnotherModalComponent() )
        {
            auto* modal = juce::Component::getCurrentlyModalComponent();
            return "blocked by modal component " + ( modal != nullptr ? nameOf( *modal ) + " (" + typeid( *modal ).name() + ")" : juce::String( "?" ) );
        }

        busy = true;
        auto finish = [this, Done]() {
            busy = false;
            Done();
        };

        // Buttons: the same path as a macOS accessibility press (AXPress -> triggerClick), which
        // flashes the button down and up, so the panel keys send their press packets as usual.
        auto* button = dynamic_cast<juce::Button*>( c );
        if( button == nullptr )
            button = c->findParentComponentOfClass<juce::Button>();
        if( button != nullptr )
        {
            button->triggerClick();
            juce::Timer::callAfterDelay( 200, finish );
            return {};
        }

        // Anything else: mouseDown now, mouseUp 80 ms later, at the touched point.
        // First what JUCE does on a click: keyboard focus goes to the touched component or the
        // nearest parent that wants it. That is how a touched control receives the jog wheel,
        // + / -, PUSH and the slider. JUCE refuses focus while the window is not the key window
        // (screen locked), so the control is also recorded as the simulator's focus target
        // (ui::simFocusTarget, kick_ControlBase.h).
        {
            juce::Component* f = c;
            while( f != nullptr && !f->getWantsKeyboardFocus() )
                f = f->getParentComponent();
            if( f != nullptr )
            {
                f->grabKeyboardFocus();
                ui::simFocusTarget() = f;
                log( "focus " + nameOf( *f ) + " (" + typeid( *f ).name() + ")" );
            }
        }
        const auto pos  = touchedPos;
        const auto now  = juce::Time::getCurrentTime();
        auto       make = [c, pos, now]( juce::ModifierKeys Mods ) {
            return juce::MouseEvent( juce::Desktop::getInstance().getMainMouseSource(), pos, Mods,
                                     juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX,
                                     juce::MouseInputSource::defaultTiltY, c, c, juce::Time::getCurrentTime(), pos, now, 1, false );
        };
        c->mouseDown( make( juce::ModifierKeys( juce::ModifierKeys::leftButtonModifier ) ) );
        juce::Component::SafePointer<juce::Component> safe( c );
        juce::Timer::callAfterDelay( 80, [safe, make, finish]() {
            if( safe != nullptr )
                safe->mouseUp( make( {} ) );
            finish();
        } );
        return {};
    }

    juce::String SimControl::snapshot( const juce::File& Out )
    {
        auto write = [this]( juce::Component& C, const juce::File& F, float Scale ) -> juce::String {
            auto img = C.createComponentSnapshot( C.getLocalBounds(), true, Scale );
            F.deleteFile();
            juce::FileOutputStream os( F );
            if( !os.openedOk() || !juce::PNGImageFormat().writeImageToStream( img, os ) )
                return "cannot write " + F.getFullPathName();
            log( "shot " + F.getFullPathName() + " " + nameOf( C ) + " " + C.getScreenBounds().toString() );
            return {};
        };
        auto sibling = [&Out]( const juce::String& Suffix ) {
            return Out.getSiblingFile( Out.getFileNameWithoutExtension() + "-" + Suffix + ".png" );
        };

        // OUT.png: the panel window; OUT-screen.png: the Kick screen at its native 1024x600;
        // OUT-<n>.png: every other showing window (the app's full-screen popups).
        auto wins = windows( panel );
        auto r    = write( *wins[0], Out, 1.0f );
        if( r.isEmpty() )
            r = write( screen, sibling( "screen" ), 1.0f );
        for( int i = 1; i < wins.size() && r.isEmpty(); ++i )
            r = write( *wins[i], sibling( juce::String( i ) ), 1.0f );
        return r;
    }
} // namespace sim
