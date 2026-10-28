/*
 * Copyright (c) 2022 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Front-panel geometry. All numbers are canvas units (Simulator::canvasW x canvasH) taken from a
 * top-down photo of the unit, so the simulator keeps the proportions of the hardware:
 *
 *   camera / MASTER / REC / LINK      display (bezel + screen)        P1..P4
 *   slider, FULL LEVEL, 16 LEVELS,    16 pads (1 bottom-left)         BANK, function keys,
 *   ERASE                                                              jog wheel, transport
 */

#include "kick_Simulator.h"
#include <contracts/dcl/dcl_contracts_helper.h>

namespace sim
{
    namespace
    {
        constexpr int velocityDefault = 80, aftertouch = 100;

        // Colours of the unit...
        const juce::Colour body( 0xff1e1e1e ), bodyEdge( 0xff0c0c0c ), bezel( 0xff0a0a0a ), keyFace( 0xff232323 ), keyFacePressed( 0xff383838 ), keyEdge( 0xff3c3c3c ), print( 0xffb9b9b9 ), printDim( 0xff6f6f6f );
        const juce::Colour ledBlue( 0xff3d9bff ), ledMagenta( 0xffff3fd0 ), ledGreen( 0xff35e07a ), ledRed( 0xffff3b3b ), ledWhite( 0xfff2f2f2 ), ledCyan( 0xff5ff2ff );

        // Display cut-out: the 1024x600 KickHome is scaled by `screenScale` into this rectangle.
        constexpr int   screenX = 260, screenY = 95, screenW = 730;
        constexpr float screenScale = screenW / 1024.f;
        constexpr int   screenH     = (int)( 600 * screenScale + 0.5f ); // 428

        CPktMgr::HWMsg_t makeMsg( CPktMgr::enumPacketType Type, int Cmd, std::vector<int> Data )
        {
            CPktMgr::HWMsg_t m;
            m.pktType = Type;
            m.pktCmd  = Cmd;
            m.pktDir  = CPktMgr::EN_PKT_REQUEST;
            m.data    = std::move( Data );
            return m;
        }

        juce::Font panelFont( float Size, bool Bold = true )
        {
            return juce::Font( "Helvetica", Size, Bold ? juce::Font::bold : juce::Font::plain );
        }
    } // namespace

    void sendButtonPacket( int Id, int BtnState )
    {
        SimulatorHub::get().sendToApp( makeMsg( CPktMgr::EN_PKT_TYPE_BUTTON, DCC::EN_BTN_STATUS, { Id, BtnState, velocityDefault, aftertouch } ) );
    }

    namespace
    {
        std::atomic<bool> shiftHeld{ false };

        // Key -> its SHIFT function, as printed under the keys of the unit.
        const std::map<int, int> shiftMap = {
            { DCC::EN_MAINMODE, DCC::EN_GRID },
            { DCC::EN_BROWSE, DCC::EN_SAVE },
            { DCC::EN_TRACK_MIX, DCC::EN_PAD_MIX },
            { DCC::EN_TRACK_MUTE, DCC::EN_PAD_MUTE },
            { DCC::EN_NEXT_SEQ, DCC::EN_FX },
            { DCC::EN_TAP_TEMPO, DCC::EN_MASTER },
            { DCC::EN_NOTE_REPEAT, DCC::EN_LATCH },
            { DCC::EN_MENU, DCC::EN_PREF },
            { DCC::EN_STEP_SEQ, DCC::EN_AUTOMATION },
            { DCC::EN_TC, DCC::EN_ON_OFF },
            { DCC::EN_SAMPLER, DCC::EN_LOOPER },
            { DCC::EN_SAMPLE_EDIT, DCC::EN_LINK_EDIT },
            { DCC::EN_PROG_EDIT, DCC::EN_MIDI_CONTROL },
            { DCC::EN_FULL_LEVEL, DCC::EN_HALF_LEVEL },
            { DCC::EN_16LEVELS, DCC::EN_NOTES },
            { DCC::EN_BANK_A, DCC::EN_BANK_E },
            { DCC::EN_BANK_B, DCC::EN_BANK_F },
            { DCC::EN_BANK_C, DCC::EN_BANK_G },
            { DCC::EN_BANK_D, DCC::EN_BANK_H },
        };
    } // namespace

    void setShiftHeld( bool Held )
    {
        shiftHeld.store( Held );
        contracts::shiftBtnState.store( Held ); // what the keyboard detector does on desktop builds
        sendButtonPacket( DCC::EN_SHIFT, Held ? DCC::BTN_PRESSED : DCC::BTN_RELEASED );
    }

    bool isShiftHeld() { return shiftHeld.load(); }

    int shiftedIdFor( int Id )
    {
        auto it = shiftMap.find( Id );
        return it == shiftMap.end() ? Id : it->second;
    }

    //==============================================================================================
    juce::Colour padColourFor( int colourId )
    {
        switch( colourId )
        {
            case DCC::padColourDarkRed: return juce::Colour( 0xffb00020 );
            case DCC::padColourVividPurple: return juce::Colour( 0xffa020ff );
            case DCC::padColourDarkYellow: return juce::Colour( 0xffd8c000 );
            case DCC::padColourlightGreen1: return juce::Colour( 0xff9cf29c );
            case DCC::padColourlightGreen3: return juce::Colour( 0xff40d080 );
            case DCC::padColourlightGreen4: return juce::Colour( 0xff00d860 );
            case DCC::PadColourLightSkyBlue: return juce::Colour( 0xff87cefa );
            case DCC::PadColourAqua: return juce::Colour( 0xff20f0f0 );
            case DCC::PadColourBlue1: return juce::Colour( 0xff2f8fff );
            case DCC::PadColourPink: return juce::Colour( 0xffffb0d0 );
            case DCC::PadColourPink1: return juce::Colour( 0xffff60b0 );
            case DCC::PadColourPink2: return juce::Colour( 0xffff1f8f );
            case DCC::PadColourRed: return juce::Colour( 0xffff2a2a );
            case DCC::PadColourOrange1: return juce::Colour( 0xffffa84a );
            case DCC::PadColourOrange: return juce::Colour( 0xffff8c00 );
            case DCC::PadColourNeutralWhite: return juce::Colour( 0xfff4f4f4 );
            case DCC::PadColourNeutralGrey: return juce::Colour( 0xff9a9a9a );
            case DCC::PadColourTrans:
            default: return juce::Colour( 0xff2b2b2b );
        }
    }

    //==============================================================================================
    KnobLookAndFeel::KnobLookAndFeel( Style S )
        : style( S ) {}

    void KnobLookAndFeel::drawRotarySlider( juce::Graphics& g, int x, int y, int width, int height, float sliderPos, float startAngle, float endAngle, juce::Slider& )
    {
        drawKnob( g, juce::Rectangle<float>( (float)x, (float)y, (float)width, (float)height ), startAngle + sliderPos * ( endAngle - startAngle ), style );
    }

    void KnobLookAndFeel::drawKnob( juce::Graphics& g, juce::Rectangle<float> Area, float angle, Style style, bool glow )
    {
        auto       area   = Area.reduced( 2.f );
        const auto radius = juce::jmin( area.getWidth(), area.getHeight() ) * 0.5f;
        const auto centre = area.getCentre();

        if( style == Style::Jog )
        {
            // Outer ring with red glow (the unit lights the ring around the jog wheel).
            auto ring = juce::Rectangle<float>( radius * 2.f, radius * 2.f ).withCentre( centre );
            g.setColour( juce::Colour( 0xff111111 ) );
            g.fillEllipse( ring );
            if( glow )
            {
                g.setColour( ledRed.withAlpha( 0.55f ) );
                g.drawEllipse( ring.reduced( radius * 0.30f ), radius * 0.08f );
                g.setColour( ledRed.withAlpha( 0.18f ) );
                g.drawEllipse( ring.reduced( radius * 0.24f ), radius * 0.16f );
            }
            auto knob = ring.reduced( radius * 0.40f );
            g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff3a3a3a ), knob.getX(), knob.getY(), juce::Colour( 0xff101010 ), knob.getRight(), knob.getBottom(), true ) );
            g.fillEllipse( knob );
            g.setColour( juce::Colour( 0xff555555 ) );
            g.drawEllipse( knob, 1.5f );
            // Knurl.
            const float kr = knob.getWidth() * 0.5f;
            for( int i = 0; i < 24; ++i )
            {
                const float a = angle + (float)i * juce::MathConstants<float>::twoPi / 24.f;
                g.setColour( juce::Colour( 0xff2a2a2a ) );
                g.drawLine( centre.x + std::sin( a ) * kr * 0.80f, centre.y - std::cos( a ) * kr * 0.80f, centre.x + std::sin( a ) * kr * 0.98f, centre.y - std::cos( a ) * kr * 0.98f, 2.f );
            }
            g.setColour( ledRed );
            g.fillEllipse( juce::Rectangle<float>( 6.f, 6.f ).withCentre( { centre.x + std::sin( angle ) * kr * 0.55f, centre.y - std::cos( angle ) * kr * 0.55f } ) );
            return;
        }

        // Pots and encoders: a dark cap with a pointer.
        auto cap = juce::Rectangle<float>( radius * 2.f, radius * 2.f ).withCentre( centre );
        g.setColour( juce::Colours::black.withAlpha( 0.6f ) );
        g.fillEllipse( cap.translated( 1.5f, 2.5f ) );
        g.setGradientFill( juce::ColourGradient( juce::Colour( style == Style::Encoder ? 0xff6a6a6a : 0xff3c3c3c ), cap.getX(), cap.getY(), juce::Colour( 0xff0e0e0e ), cap.getRight(), cap.getBottom(), true ) );
        g.fillEllipse( cap );
        g.setColour( juce::Colour( 0xff666666 ) );
        g.drawEllipse( cap, 1.2f );
        if( style == Style::Encoder )
        {
            g.setColour( juce::Colour( 0xff9a9a9a ) );
            g.fillEllipse( cap.reduced( radius * 0.35f ) );
        }
        g.setColour( style == Style::Encoder ? juce::Colour( 0xff202020 ) : juce::Colours::white );
        g.drawLine( centre.x + std::sin( angle ) * radius * 0.35f, centre.y - std::cos( angle ) * radius * 0.35f, centre.x + std::sin( angle ) * radius * 0.92f, centre.y - std::cos( angle ) * radius * 0.92f, 2.5f );
    }

    //==============================================================================================
    EndlessKnob::EndlessKnob( KnobLookAndFeel::Style S, float PixelsPerStep )
        : style( S )
        , pixelsPerStep( PixelsPerStep )
    {
        setMouseCursor( juce::MouseCursor::UpDownLeftRightResizeCursor );
    }

    void EndlessKnob::paint( juce::Graphics& g )
    {
        KnobLookAndFeel::drawKnob( g, getLocalBounds().toFloat(), angle, style );
    }

    void EndlessKnob::mouseDown( const juce::MouseEvent& e )
    {
        lastPos     = e.position;
        accumulated = 0.f;
    }

    void EndlessKnob::mouseDrag( const juce::MouseEvent& e )
    {
        // Up or right = clockwise, like dragging the rim of the knob.
        const auto pixels = ( lastPos.y - e.position.y ) + ( e.position.x - lastPos.x );
        lastPos           = e.position;
        turn( pixels );
    }

    void EndlessKnob::mouseWheelMove( const juce::MouseEvent&, const juce::MouseWheelDetails& wheel )
    {
        turn( ( wheel.deltaY + wheel.deltaX ) * ( wheel.isReversed ? -1.f : 1.f ) * 60.f );
    }

    void EndlessKnob::turn( float Pixels )
    {
        if( Pixels == 0.f ) return;

        // The pointer keeps turning for ever: one step is a small, fixed angle, and the angle wraps.
        angle = std::fmod( angle + Pixels / pixelsPerStep * 0.12f + juce::MathConstants<float>::twoPi, juce::MathConstants<float>::twoPi );

        accumulated += Pixels;
        const int steps = (int)( accumulated / pixelsPerStep );
        if( steps != 0 )
        {
            accumulated -= (float)steps * pixelsPerStep;
            if( onStep ) onStep( steps );
        }
        repaint();
    }

    //==============================================================================================
    PadButton::PadButton( int Index )
        : juce::Button( "Pad " + juce::String( Index + 1 ) )
        , index( Index )
    {
        setMouseCursor( juce::MouseCursor::PointingHandCursor );
    }

    void PadButton::set( int ColourId, int State, int Velocity )
    {
        colourId   = ColourId;
        state      = State;
        brightness = juce::jlimit( 0, 127, Velocity );
        repaint();
    }

    void PadButton::paintButton( juce::Graphics& g, bool, bool pressed )
    {
        pressed = pressed || scriptedDown;
        auto       bounds = getLocalBounds().toFloat().reduced( 1.f );
        // Dimmer for a lower velocity (STEP SEQ steps, 16 LEVELS): towards the unlit pad colour.
        const auto base   = padColourFor( colourId ).interpolatedWith( juce::Colour( 0xff2b2b2b ), 0.8f * ( 1.f - (float)brightness / 127.f ) );
        const bool lit    = state != DCC::StateOff && colourId != DCC::PadColourTrans;

        // Rubber pad.
        g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff303030 ), bounds.getX(), bounds.getY(), juce::Colour( 0xff161616 ), bounds.getRight(), bounds.getBottom(), false ) );
        g.fillRoundedRectangle( bounds, 5.f );

        if( lit )
        {
            // Light leaks around the edge, like the unit.
            g.setColour( base.withAlpha( pressed ? 0.95f : 0.75f ) );
            g.drawRoundedRectangle( bounds.reduced( 1.5f ), 5.f, 3.f );
            g.setColour( base.withAlpha( pressed ? 0.35f : 0.12f ) );
            g.fillRoundedRectangle( bounds.reduced( 3.f ), 4.f );
        }
        else
        {
            g.setColour( juce::Colour( pressed ? 0xff6a6a6a : 0xff3a3a3a ) );
            g.drawRoundedRectangle( bounds.reduced( 1.f ), 5.f, 1.5f );
        }
    }

    void PadButton::send( int BtnState, int Velocity )
    {
        SimulatorHub::get().sendToApp( makeMsg( CPktMgr::EN_PKT_TYPE_PAD, DCC::EN_PAD_STATE, { DCC::EN_PAD1 + index, BtnState, Velocity, aftertouch } ) );
    }

    void PadButton::scriptedHold( bool Down, int Velocity )
    {
        if( Down == scriptedDown ) return;
        scriptedDown = Down;
        if( Down && Velocity > 0 ) velocity = juce::jlimit( 1, 127, Velocity );
        send( Down ? DCC::BTN_PRESSED : DCC::BTN_RELEASED, Down ? velocity : 0 );
        repaint();
    }

    void PadButton::mouseDown( const juce::MouseEvent& e )
    {
        velocity = juce::jlimit( 1, 127, 20 + (int)( 107.f * e.position.y / (float)juce::jmax( 1, getHeight() ) ) );
        juce::Button::mouseDown( e );
    }

    void PadButton::buttonStateChanged()
    {
        // Press / release follow the button's down state, so both the mouse and accessibility
        // (triggerClick) drive the pad.
        const bool down = isDown();
        if( down != wasDown )
        {
            wasDown = down;
            send( down ? DCC::BTN_PRESSED : DCC::BTN_RELEASED, down ? velocity : 0 );
        }
    }

    //==============================================================================================
    SurfaceButton::SurfaceButton( const juce::String& Caption, int Id, juce::Colour LedColour, bool Toggle, Shape S, Icon I )
        : juce::Button( Caption.isNotEmpty() ? Caption.replace( "\n", " " ) : "key " + juce::String( Id ) )
        , caption( Caption )
        , id( Id )
        , ledColour( LedColour )
        , toggle( Toggle )
        , shape( S )
        , icon( I )
    {
        setMouseCursor( juce::MouseCursor::PointingHandCursor );
        setClickingTogglesState( Toggle );
    }

    void SurfaceButton::setLed( bool On )
    {
        if( toggle ) return; // SHIFT keeps its own state.
        led = On;
        repaint();
    }

    void SurfaceButton::buttonStateChanged()
    {
        // The unit reports keys on press, not on release. Following the down state means the
        // mouse and accessibility clicks (triggerClick) behave the same.
        const bool down = isDown();
        if( down != wasDown && !toggle ) press( down );
        wasDown = down;
    }

    void SurfaceButton::press( bool Down )
    {
        if( Down )
        {
            sentId = isShiftHeld() ? shiftedIdFor( id ) : id;
            sendButtonPacket( sentId, DCC::BTN_PRESSED );
        }
        else if( sendsRelease() && sentId >= 0 )
            sendButtonPacket( sentId, DCC::BTN_RELEASED );
    }

    void SurfaceButton::scriptedHold( bool Down )
    {
        if( Down == scriptedDown || toggle ) return;
        scriptedDown = Down;
        press( Down );
        repaint();
    }

    void SurfaceButton::clicked()
    {
        // SHIFT is a toggle here (the unit's is momentary): while it is on, keys send their
        // secondary function. Also the path taken by accessibility clicks.
        if( toggle ) setShiftHeld( getToggleState() );
    }

    void SurfaceButton::drawIcon( juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c ) const
    {
        g.setColour( c );
        const auto cx = r.getCentreX(), cy = r.getCentreY(), s = juce::jmin( r.getWidth(), r.getHeight() ) * 0.5f;
        juce::Path p;
        switch( icon )
        {
            case Icon::Record: g.fillEllipse( juce::Rectangle<float>( s * 1.2f, s * 1.2f ).withCentre( { cx, cy } ) ); break;
            case Icon::Overdub:
                g.drawEllipse( juce::Rectangle<float>( s * 1.3f, s * 1.3f ).withCentre( { cx, cy } ), s * 0.22f );
                g.fillEllipse( juce::Rectangle<float>( s * 0.5f, s * 0.5f ).withCentre( { cx, cy } ) );
                break;
            case Icon::Stop: g.fillRect( juce::Rectangle<float>( s * 1.1f, s * 1.1f ).withCentre( { cx, cy } ) ); break;
            case Icon::Play:
                p.addTriangle( cx - s * 0.5f, cy - s * 0.65f, cx - s * 0.5f, cy + s * 0.65f, cx + s * 0.7f, cy );
                g.fillPath( p );
                break;
            case Icon::PlayStart:
                p.addTriangle( cx - s * 0.75f, cy - s * 0.6f, cx - s * 0.75f, cy + s * 0.6f, cx + s * 0.25f, cy );
                g.fillPath( p );
                g.fillRect( juce::Rectangle<float>( cx + s * 0.4f, cy - s * 0.6f, s * 0.3f, s * 1.2f ) );
                break;
            case Icon::Back:
                p.addArc( cx - s * 0.55f, cy - s * 0.55f, s * 1.1f, s * 1.1f, juce::MathConstants<float>::pi * 0.15f, juce::MathConstants<float>::pi * 1.75f, true );
                g.strokePath( p, juce::PathStrokeType( s * 0.2f ) );
                p.clear();
                p.addTriangle( cx - s * 0.55f, cy - s * 0.9f, cx - s * 0.55f, cy - s * 0.2f, cx - s * 1.0f, cy - s * 0.55f );
                g.fillPath( p );
                break;
            case Icon::Copy:
                g.drawRect( juce::Rectangle<float>( cx - s * 0.6f, cy - s * 0.6f, s * 0.9f, s * 0.9f ), s * 0.15f );
                g.drawRect( juce::Rectangle<float>( cx - s * 0.25f, cy - s * 0.25f, s * 0.9f, s * 0.9f ), s * 0.15f );
                break;
            case Icon::Minus: g.fillRect( juce::Rectangle<float>( s * 1.2f, s * 0.22f ).withCentre( { cx, cy } ) ); break;
            case Icon::Plus:
                g.fillRect( juce::Rectangle<float>( s * 1.2f, s * 0.22f ).withCentre( { cx, cy } ) );
                g.fillRect( juce::Rectangle<float>( s * 0.22f, s * 1.2f ).withCentre( { cx, cy } ) );
                break;
            case Icon::None:
            default: break;
        }
    }

    void SurfaceButton::paintButton( juce::Graphics& g, bool, bool pressed )
    {
        pressed = pressed || scriptedDown;
        auto       r   = getLocalBounds().toFloat().reduced( 1.f );
        const bool lit = led || ( toggle && getToggleState() );
        const auto c   = lit ? ledColour : ledColour.withAlpha( 0.32f );

        if( shape == Shape::Round )
        {
            g.setColour( juce::Colours::black.withAlpha( 0.6f ) );
            g.fillEllipse( r.translated( 1.f, 2.f ) );
            g.setGradientFill( juce::ColourGradient( juce::Colour( pressed ? 0xff3a3a3a : 0xff2a2a2a ), r.getX(), r.getY(), juce::Colour( 0xff0e0e0e ), r.getRight(), r.getBottom(), true ) );
            g.fillEllipse( r );
            g.setColour( keyEdge );
            g.drawEllipse( r, 1.2f );
            if( lit )
            {
                g.setColour( ledColour.withAlpha( 0.25f ) );
                g.fillEllipse( r.reduced( r.getWidth() * 0.18f ) );
            }
            drawIcon( g, r.reduced( r.getWidth() * 0.28f ), c );
            return;
        }

        g.setColour( juce::Colours::black.withAlpha( 0.5f ) );
        g.fillRoundedRectangle( r.translated( 0.f, 1.5f ), 4.f );
        g.setColour( pressed ? keyFacePressed : keyFace );
        g.fillRoundedRectangle( r, 4.f );
        g.setColour( keyEdge );
        g.drawRoundedRectangle( r, 4.f, 1.f );

        if( icon != Icon::None )
        {
            drawIcon( g, r.reduced( r.getWidth() * 0.25f ), c );
        }
        else
        {
            if( lit )
            {
                g.setColour( ledColour.withAlpha( 0.10f ) );
                g.fillRoundedRectangle( r.reduced( 2.f ), 3.f );
            }
            g.setColour( c );
            const auto lines = juce::StringArray::fromLines( caption ).size();
            g.setFont( panelFont( juce::jlimit( 9.f, 13.f, r.getHeight() / ( lines > 1 ? 3.2f : 2.6f ) ) ) );
            g.drawFittedText( caption, getLocalBounds().reduced( 3 ), juce::Justification::centred, 2, 0.9f );
        }

        if( toggle )
        {
            // SHIFT has a printed line under it.
            g.setColour( lit ? ledColour : printDim );
            g.fillRect( r.getX() + 4.f, r.getBottom() - 3.f, r.getWidth() - 8.f, 1.5f );
        }
    }

    //==============================================================================================
    Caption::Caption( const juce::String& Text, int Id, juce::Justification J )
        : juce::Button( Text )
        , text( Text )
        , id( Id )
        , just( J )
    {
        if( id >= 0 ) setMouseCursor( juce::MouseCursor::PointingHandCursor );
        setInterceptsMouseClicks( id >= 0, false );
        setTriggeredOnMouseDown( true );
    }

    void Caption::paintButton( juce::Graphics& g, bool, bool )
    {
        g.setColour( print );
        g.setFont( panelFont( juce::jlimit( 8.f, 12.f, (float)getHeight() * 0.85f ) ) );
        g.drawFittedText( text, getLocalBounds(), just, 1, 0.8f );
    }

    void Caption::clicked()
    {
        // A printed caption is "SHIFT + key": hold SHIFT, press the key (which the surface
        // reports as the secondary function), release SHIFT. The on-screen SHIFT toggle is left
        // as it was.
        if( id < 0 ) return;
        const bool wasHeld = isShiftHeld();
        if( !wasHeld ) setShiftHeld( true );
        sendButtonPacket( id, DCC::BTN_PRESSED );
        if( !wasHeld ) setShiftHeld( false );
    }

    //==============================================================================================
    Simulator::Simulator()
    {
        setOpaque( true );
        loadLogo();

        // The real device screen, scaled into the bezel cut-out (see resized()).
        addAndMakeVisible( kick_ui::KickHome::Instance() );
        // Full-screen popups, dialogs and notifications go over it, not on the desktop.
        ui::simScreen() = &kick_ui::KickHome::Instance();

        // Pads...
        for( int i = 0; i < 16; ++i )
            addAndMakeVisible( *pads.emplace_back( std::make_unique<PadButton>( i ) ) );
        for( int i = 0; i < 16; ++i )
            addCaption( juce::String( i + 1 ), -1, juce::Justification::centredLeft );

        // Left column...
        addButton( "LINK", DCC::EN_LINK, ledBlue );
        addButton( "FULL\nLEVEL", DCC::EN_FULL_LEVEL, ledMagenta );
        addCaption( "HALF LVL", DCC::EN_HALF_LEVEL );
        addButton( "16\nLEVELS", DCC::EN_16LEVELS, ledMagenta );
        addCaption( "NOTES", DCC::EN_NOTES );
        addButton( "ERASE", DCC::EN_ERASE, ledMagenta );

        // Banks...
        const juce::Colour bankColours[4] = { ledBlue, ledRed, ledBlue, ledRed };
        for( int b = 0; b < 4; ++b )
        {
            addButton( juce::String::charToString( (juce::juce_wchar)( 'A' + b ) ), DCC::EN_BANK_A + b, bankColours[b] );
            addCaption( juce::String::charToString( (juce::juce_wchar)( 'E' + b ) ), DCC::EN_BANK_E + b );
        }

        // Function keys, row 1 and their SHIFT functions...
        addButton( "MAIN", DCC::EN_MAINMODE, ledBlue );
        addCaption( "GRID", DCC::EN_GRID );
        addButton( "BROWSE", DCC::EN_BROWSE, ledMagenta );
        addCaption( "SAVE", DCC::EN_SAVE );
        addButton( "TRACK\nMIX", DCC::EN_TRACK_MIX, ledMagenta );
        addCaption( "PAD MIX", DCC::EN_PAD_MIX );
        addButton( "TRACK\nMUTE", DCC::EN_TRACK_MUTE, ledBlue );
        addCaption( "PAD MUTE", DCC::EN_PAD_MUTE );
        addButton( "NEXT\nSEQ", DCC::EN_NEXT_SEQ, ledBlue );
        addCaption( "FX", DCC::EN_FX );

        // Around the jog wheel...
        addButton( "TAP\nTEMPO", DCC::EN_TAP_TEMPO, ledMagenta );
        addCaption( "MASTER", DCC::EN_MASTER );
        addButton( "NOTE\nREPEAT", DCC::EN_NOTE_REPEAT, ledBlue );
        addCaption( "LATCH", DCC::EN_LATCH );
        addButton( "MENU", DCC::EN_MENU, ledGreen );
        addCaption( "PREFS", DCC::EN_PREF );
        addButton( "SHIFT", DCC::EN_SHIFT, ledGreen, true );
        addButton( "", DCC::EN_BACK, ledWhite, false, SurfaceButton::Shape::Key, SurfaceButton::Icon::Back );
        addButton( "", DCC::EN_COPY, ledWhite, false, SurfaceButton::Shape::Key, SurfaceButton::Icon::Copy );
        addButton( "", DCC::EN_MINUS, ledWhite, false, SurfaceButton::Shape::Key, SurfaceButton::Icon::Minus );
        addButton( "", DCC::EN_PLUS, ledWhite, false, SurfaceButton::Shape::Key, SurfaceButton::Icon::Plus );
        addButton( "PUSH", DCC::EN_KNOB_ENTER, ledWhite ); // the jog wheel push (ENTER); drawn as a small key under the wheel

        // Function keys, row 2...
        addButton( "STEP\nSEQ", DCC::EN_STEP_SEQ, ledMagenta );
        addCaption( "AUTO", DCC::EN_AUTOMATION );
        addButton( "TC", DCC::EN_TC, ledMagenta );
        addCaption( "ON/OFF", DCC::EN_ON_OFF );
        addButton( "SAMPLER", DCC::EN_SAMPLER, ledBlue );
        addCaption( "LOOP", DCC::EN_LOOPER );
        addButton( "SAMPLE\nEDIT", DCC::EN_SAMPLE_EDIT, ledMagenta );
        addCaption( "LINK EDIT", DCC::EN_LINK_EDIT );
        addButton( "PROG\nEDIT", DCC::EN_PROG_EDIT, ledMagenta );
        addCaption( "MIDI CTRL", DCC::EN_MIDI_CONTROL );

        // Transport...
        addButton( "", DCC::EN_RECORD, ledWhite, false, SurfaceButton::Shape::Round, SurfaceButton::Icon::Record );
        addButton( "", DCC::EN_OVERDUB, ledGreen, false, SurfaceButton::Shape::Round, SurfaceButton::Icon::Overdub );
        addButton( "", DCC::EN_STOP, ledCyan, false, SurfaceButton::Shape::Round, SurfaceButton::Icon::Stop );
        addButton( "", DCC::EN_PLAY, ledGreen, false, SurfaceButton::Shape::Round, SurfaceButton::Icon::Play );
        addButton( "", DCC::EN_PLAYSTART, ledGreen, false, SurfaceButton::Shape::Round, SurfaceButton::Icon::PlayStart );

        // Printed labels...
        addCaption( "MASTER" );
        addCaption( "MIN", -1, juce::Justification::centredLeft );
        addCaption( "MAX", -1, juce::Justification::centredRight );
        addCaption( "REC" );
        addCaption( "MIN", -1, juce::Justification::centredLeft );
        addCaption( "MAX", -1, juce::Justification::centredRight );
        for( int i = 0; i < 4; ++i ) addCaption( "P" + juce::String( i + 1 ) );
        addCaption( "BANK" );

        // MASTER / REC are analogue pots on the unit; here they turn but control nothing.
        setupPot( master, 0.8 );
        setupPot( rec, 0.5 );
        master.setTooltip( "Analogue master level pot on the unit: no function in the simulator" );
        rec.setTooltip( "Analogue record level pot on the unit: no function in the simulator" );

        // Q-Link encoders P1..P4 and the jog wheel: endless, like the unit. The app only ever
        // receives a direction and a step count.
        // Named for scripts (SimControl "turn P1 3", "turn JOG -1", "slider 40").
        for( int i = 0; i < 4; ++i )
        {
            setupEncoder( qlinks[i], DCC::EN_KNOB_1 + i, DCC::EN_KNOB_QLINK );
            qlinks[i].setName( "P" + juce::String( i + 1 ) );
        }
        setupEncoder( jog, DCC::EN_KNOB_CENTER, DCC::EN_KNOB_ROTARY );
        jog.setName( "JOG" );
        slider.setName( "SLIDER" );

        // Slider: absolute, 0..100 %...
        slider.setSliderStyle( juce::Slider::LinearVertical );
        slider.setTextBoxStyle( juce::Slider::NoTextBox, false, 0, 0 );
        slider.setColour( juce::Slider::trackColourId, juce::Colour( 0xff101010 ) );
        slider.setColour( juce::Slider::backgroundColourId, juce::Colour( 0xff101010 ) );
        slider.setColour( juce::Slider::thumbColourId, juce::Colour( 0xff2c2c2c ) );
        slider.setRange( 0, 100, 1 );
        slider.setValue( 100, juce::dontSendNotification );
        slider.onValueChange = [this] {
            SimulatorHub::get().sendToApp( makeMsg( CPktMgr::EN_PKT_TYPE_SLIDER, DCC::EN_SLIDER_TOUCH_INPUT, { (int)slider.getValue() } ) );
        };
        addAndMakeVisible( slider );

        // Application -> surface...
        fromAppConnection = SimulatorHub::get().fromApp.connect( [this]( CPktMgr::HWMsg_t& m ) { handleFromApp( m ); } );

        // Pretend the control surface just came up: the app answers with a COMM_INIT and
        // thereafter expects a periodic "comm live" ping.
        startTimer( 1000 );

        // Last: this triggers resized(), which needs every control above to exist.
        setSize( canvasW, canvasH );
    }

    Simulator::~Simulator()
    {
        stopTimer();
        auto& home = kick_ui::KickHome::Instance();
        ui::simScreen() = nullptr;
        home.setTransform( {} );
        removeChildComponent( &home );
        for( auto* s : { &master, &rec } )
            s->setLookAndFeel( nullptr );
    }

    void Simulator::loadLogo()
    {
        // The artwork is dark on white; turn darkness into alpha so it prints in panel grey on the
        // black bezel, then crop to the mark.
        if( !logoFile.existsAsFile() ) return;
        auto src = juce::ImageCache::getFromFile( logoFile );
        if( !src.isValid() ) return;

        juce::Image out( juce::Image::ARGB, src.getWidth(), src.getHeight(), true );
        juce::Rectangle<int> box;
        {
            juce::Image::BitmapData in( src, juce::Image::BitmapData::readOnly );
            juce::Image::BitmapData o( out, juce::Image::BitmapData::writeOnly );
            for( int y = 0; y < src.getHeight(); ++y )
                for( int x = 0; x < src.getWidth(); ++x )
                {
                    const auto p = in.getPixelColour( x, y );
                    const auto a = juce::jlimit( 0.f, 1.f, ( 1.f - p.getBrightness() ) * p.getFloatAlpha() * 1.15f );
                    o.setPixelColour( x, y, print.withAlpha( a ) );
                    if( a > 0.05f ) box = box.isEmpty() ? juce::Rectangle<int>( x, y, 1, 1 ) : box.getUnion( { x, y, 1, 1 } );
                }
        }
        logo = box.isEmpty() ? out : out.getClippedImage( box.expanded( 2 ) );
    }

    SurfaceButton& Simulator::addButton( const juce::String& Caption, int Id, juce::Colour Led, bool Toggle, SurfaceButton::Shape S, SurfaceButton::Icon I )
    {
        auto& b = *buttons.emplace_back( std::make_unique<SurfaceButton>( Caption, Id, Led, Toggle, S, I ) );
        buttonsById[Id] = &b;
        addAndMakeVisible( b );
        return b;
    }

    Caption& Simulator::addCaption( const juce::String& Text, int Id, juce::Justification J )
    {
        auto& c = *captions.emplace_back( std::make_unique<Caption>( Text, Id, J ) );
        addAndMakeVisible( c );
        return c;
    }

    void Simulator::setupPot( juce::Slider& Knob, double Value )
    {
        Knob.setSliderStyle( juce::Slider::RotaryHorizontalVerticalDrag );
        Knob.setTextBoxStyle( juce::Slider::NoTextBox, false, 0, 0 );
        Knob.setMouseDragSensitivity( 150 );
        Knob.setRange( 0.0, 1.0, 0.01 );
        Knob.setValue( Value, juce::dontSendNotification );
        Knob.setLookAndFeel( &potLnf );
        addAndMakeVisible( Knob );
    }

    void Simulator::setupEncoder( EndlessKnob& Knob, int KnobId, int Cmd )
    {
        Knob.onStep = [this, KnobId, Cmd]( int Steps ) { sendKnob( KnobId, Cmd, Steps ); };
        addAndMakeVisible( Knob );
    }

    void Simulator::sendKnob( int KnobId, int Cmd, int Delta )
    {
        const int dir = Delta >= 0 ? DCC::EN_KNOB_CLOCKWISE : DCC::EN_KNOB_ANTICLOCKWISE;
        SimulatorHub::get().sendToApp( makeMsg( CPktMgr::EN_PKT_TYPE_KNOBS, Cmd, { KnobId, dir, std::abs( Delta ), 0 } ) );
    }

    void Simulator::sendSystem( int Cmd, std::vector<int> Data )
    {
        SimulatorHub::get().sendToApp( makeMsg( CPktMgr::EN_PKT_TYPE_SYSTEM, Cmd, std::move( Data ) ) );
    }

    void Simulator::timerCallback()
    {
        if( commTicks == 0 )
        {
            sendSystem( DCC::EN_SYS_GET_SW_VER, { 0, 1 } ); // "surface firmware" 0.1
            sendSystem( DCC::EN_SYS_CHECK_COMM_STATUS, { 1 } ); // comm init
            startTimer( 5000 );
        }
        else
        {
            sendSystem( DCC::EN_SYS_CHECK_COMM_STATUS, { 2 } ); // comm live
        }
        ++commTicks;
    }

    //==============================================================================================
    void Simulator::handleFromApp( CPktMgr::HWMsg_t& msg )
    {
        // Opt-in trace of everything the app sends to the surface, for scripts (sim_smoke.sh):
        //   SimSurface: LED cmd=0 data=44,1      (type name, pktCmd, payload)
        static const bool trace = juce::SystemStats::getEnvironmentVariable( "KICK_SIM_LOG_SURFACE", {} ).isNotEmpty();
        if( trace )
        {
            static const char* types[] = { "SYSTEM", "BUTTON", "LED", "PAD", "KNOBS", "MIDI", "CV", "SLIDER" };
            juce::StringArray  bytes;
            for( auto v : msg.data ) bytes.add( juce::String( (int)v ) );
            const auto t = (int)msg.pktType;
            juce::Logger::writeToLog( "SimSurface: " + juce::String( t >= 0 && t < 8 ? types[t] : "TYPE" + juce::String( t ) ) + " cmd=" + juce::String( (int)msg.pktCmd ) + " data=" + bytes.joinIntoString( "," ) );
        }

        switch( msg.pktType )
        {
            case CPktMgr::EN_PKT_TYPE_PAD: handlePadMessage( msg ); break;
            case CPktMgr::EN_PKT_TYPE_LED: handleLedMessage( msg ); break;
            case CPktMgr::EN_PKT_TYPE_SLIDER: handleSliderMessage( msg ); break;
            default: break; // SYSTEM (tempo, reconnect...), MIDI, CV: nothing to show.
        }
    }

    void Simulator::handlePadMessage( const CPktMgr::HWMsg_t& msg )
    {
        const auto& d  = msg.data;
        auto        at = [&]( size_t i, int def = 0 ) { return i < d.size() ? d[i] : def; };

        switch( msg.pktCmd )
        {
            case DCC::EN_PAD_SET_SINGLE_PAD: // pad, state, colour
            case DCC::EN_PAD_SET_BLINKING: // pad, state, on colour, off colour
                if( at( 0 ) >= 0 && at( 0 ) < 16 ) pads[(size_t)at( 0 )]->set( at( 2 ), at( 1 ) );
                break;

            case DCC::EN_PAD_SET_STEP_SINGLE_PAD: // pad, state, colour, velocity
                if( at( 0 ) >= 0 && at( 0 ) < 16 ) pads[(size_t)at( 0 )]->set( at( 2 ), at( 1 ), at( 3, 127 ) );
                break;

            case DCC::EN_PAD_SET_ALL_SINGLE_COLOUR: // state, colour
                for( auto& p : pads ) p->set( at( 1 ), at( 0 ) );
                break;

            case DCC::EN_PAD_SET_ALL_MULTI_COLOUR: // state, colour x16
                for( size_t i = 0; i < 16; ++i ) pads[i]->set( at( 1 + i ), at( 0 ) );
                break;

            case DCC::EN_PAD_SET_STEP_ALL_PAD: // state, colour x16, velocity x16 (STEP SEQ, 16 LEVELS)
                for( size_t i = 0; i < 16; ++i ) pads[i]->set( at( 1 + i ), at( 0 ), at( 17 + i, 127 ) );
                break;

            case DCC::EN_PAD_SET_COLOUR_STATE: // colour x16, state x16
                for( size_t i = 0; i < 16; ++i ) pads[i]->set( at( i ), at( 16 + i ) );
                break;

            default: break; // settings, colour mode, aftertouch: no visual.
        }
    }

    void Simulator::handleLedMessage( const CPktMgr::HWMsg_t& msg )
    {
        const auto& d = msg.data;
        if( msg.pktCmd == DCC::EN_LED_SET_SINGLE_LED || msg.pktCmd == DCC::EN_LED_BLINK_SINGLE )
        {
            if( d.size() < 2 ) return;
            int id = d[0];
            // Banks: the app only ever sends "bank N on" (kick_leds.cpp, setBankMessage) and never
            // switches the previous bank off, so the surface shows one bank at a time. Banks E-H
            // are SHIFT + A-D and light the key they are printed under.
            if( id >= DCC::EN_BANK_A && id <= DCC::EN_BANK_H )
            {
                if( d[1] == DCC::StateOff ) return;
                for( int b = DCC::EN_BANK_A; b <= DCC::EN_BANK_D; ++b ) buttonsById[b]->setLed( false );
                buttonsById[DCC::EN_BANK_A + ( id - DCC::EN_BANK_A ) % 4]->setLed( true );
                return;
            }
            // Transport: the app lights PLAY or STOP with a "media control" message (third value 1,
            // DclContractsHelper::setMediaControlMessage) and never switches the other one off; the
            // surface shows one of PLAY / STOP / PLAY START at a time.
            if( ( id == DCC::EN_PLAY || id == DCC::EN_STOP || id == DCC::EN_PLAYSTART ) && d.size() >= 3 && d[2] == 1 && d[1] != DCC::StateOff )
            {
                for( int t : { (int)DCC::EN_STOP, (int)DCC::EN_PLAY, (int)DCC::EN_PLAYSTART } )
                {
                    ledOn[t] = false;
                    if( auto it = buttonsById.find( t ); it != buttonsById.end() )
                        it->second->setLed( false );
                }
            }
            // Other SHIFT functions with an LED (HALF LEVEL, LATCH) have no key of their own: they
            // show on the key they are printed under, which is lit while either LED is on.
            ledOn[id] = d[1] != DCC::StateOff;
            int key   = id;
            if( !buttonsById.count( key ) )
                for( const auto& [primary, button] : buttonsById )
                    if( shiftedIdFor( primary ) == id ) key = primary;
            if( auto it = buttonsById.find( key ); it != buttonsById.end() )
            {
                const int secondary = shiftedIdFor( key );
                it->second->setLed( ledOn[key] || ( secondary != key && ledOn[secondary] ) );
            }
        }
        else if( msg.pktCmd == DCC::EN_LED_SET_ALL )
        {
            const bool on = !d.empty() && d[0] != DCC::StateOff;
            for( auto& b : buttons ) b->setLed( on );
        }
    }

    void Simulator::handleSliderMessage( const CPktMgr::HWMsg_t& msg )
    {
        // The app shows a value on the slider's LED ladder: the focused control's value
        // (EN_SLIDER_BAR_LED from the bottom, EN_SLIDER_LEVEL_LED from the centre for pan-like
        // controls) or, on MAIN's Track tab, the track meter.
        if( msg.data.empty() || ( msg.pktCmd != DCC::EN_SLIDER_BAR_LED && msg.pktCmd != DCC::EN_SLIDER_LEVEL_LED ) )
            return;
        const int  value   = juce::jlimit( 0, 100, (int)msg.data[0] );
        const bool centred = msg.pktCmd == DCC::EN_SLIDER_LEVEL_LED;
        if( value != sliderLed || centred != sliderLedCentred )
        {
            sliderLed        = value;
            sliderLedCentred = centred;
            repaint( 95, 640, 30, 270 );
        }
    }

    //==============================================================================================
    void Simulator::paint( juce::Graphics& g )
    {
        g.fillAll( juce::Colours::black );

        // Body.
        auto bodyRect = getLocalBounds().toFloat().reduced( 4.f );
        g.setColour( bodyEdge );
        g.fillRoundedRectangle( bodyRect.expanded( 3.f ), 22.f );
        g.setGradientFill( juce::ColourGradient( juce::Colour( 0xff232323 ), 0.f, 0.f, juce::Colour( 0xff191919 ), 0.f, (float)getHeight(), false ) );
        g.fillRoundedRectangle( bodyRect, 20.f );

        // Display bezel with the printed logos.
        auto bezelRect = juce::Rectangle<float>( 200.f, 30.f, 840.f, 530.f );
        g.setColour( juce::Colours::black.withAlpha( 0.5f ) );
        g.fillRoundedRectangle( bezelRect.expanded( 2.f ), 10.f );
        g.setColour( bezel );
        g.fillRoundedRectangle( bezelRect, 8.f );
        if( logo.isValid() )
        {
            const float h = 30.f, w = h * (float)logo.getWidth() / (float)logo.getHeight();
            g.drawImage( logo, juce::Rectangle<float>( w, h ).withCentre( { bezelRect.getCentreX(), bezelRect.getY() + 32.f } ), juce::RectanglePlacement::centred );
        }
        else
        {
            g.setColour( print );
            g.setFont( panelFont( 22.f, false ).italicised() );
            g.drawText( "behringer", bezelRect.withHeight( 60.f ), juce::Justification::centred );
        }
        g.setColour( print );
        g.setFont( panelFont( 20.f ) );
        g.drawText( "KICK", bezelRect.withTop( bezelRect.getBottom() - 38.f ), juce::Justification::centred );

        // Camera module (top left).
        g.setColour( juce::Colour( 0xff0d0d0d ) );
        g.fillRoundedRectangle( 35.f, 40.f, 130.f, 65.f, 6.f );
        g.setColour( juce::Colour( 0xffd8b040 ) );
        g.fillRect( 62.f, 66.f, 10.f, 12.f );
        g.setColour( juce::Colour( 0xff203050 ) );
        g.fillEllipse( 95.f, 58.f, 28.f, 28.f );
        g.setColour( juce::Colour( 0xff4060a0 ) );
        g.fillEllipse( 104.f, 67.f, 10.f, 10.f );

        // MIN / MAX arcs under MASTER and REC are printed; captions do the text.

        // LINK block with its four LEDs.
        g.setColour( juce::Colour( 0xff141414 ) );
        g.fillRoundedRectangle( 35.f, 450.f, 130.f, 110.f, 6.f );
        for( int i = 0; i < 4; ++i )
        {
            g.setColour( juce::Colour( 0xff3a3a3a ) );
            g.fillRect( 58.f + (float)i * 26.f, 466.f, 10.f, 6.f );
        }

        // Slider channel with its LED ladder.
        g.setColour( juce::Colour( 0xff101010 ) );
        g.fillRoundedRectangle( 25.f, 635.f, 110.f, 270.f, 8.f );
        for( int i = 0; i < 17; ++i )
        {
            // Tick i = 0 is the top (100 %), 16 the bottom (0 %). Lit up to the app's value, or
            // from the centre to it for a centred (level / pan) value.
            const int  pos = ( 16 - i ) * 100 / 16;
            const bool lit = sliderLed >= 0
                             && ( sliderLedCentred ? ( ( pos >= 50 && pos <= sliderLed ) || ( pos <= 50 && pos >= sliderLed ) )
                                                   : ( sliderLed > 0 && pos <= sliderLed ) );
            g.setColour( lit ? ledRed : juce::Colour( 0xff444444 ) );
            g.fillRect( 104.f, 650.f + (float)i * 15.f, i % 4 == 0 ? 12.f : 8.f, 2.f );
        }

        // BANK header line.
        g.setColour( printDim );
        g.fillRect( 800.f, 614.f, 185.f, 1.5f );
        g.fillRect( 1055.f, 614.f, 155.f, 1.5f );

        // Jog wheel surround (the four keys sit on it).
        g.setColour( juce::Colour( 0xff151515 ) );
        g.fillEllipse( 880.f, 795.f, 260.f, 260.f );
    }

    void Simulator::resized()
    {
        // --- Screen -----------------------------------------------------------------------------
        auto& home = kick_ui::KickHome::Instance();
        home.setTransform( juce::AffineTransform::scale( screenScale ).translated( (float)screenX, (float)screenY ) );
        home.setBounds( 0, 0, 1024, 600 );
        juce::ignoreUnused( screenH );

        size_t cap = 0; // captions, in creation order

        // --- Pads: pad 1 bottom-left ------------------------------------------------------------
        for( int i = 0; i < 16; ++i )
        {
            const int row = 3 - i / 4, col = i % 4;
            const int x = 175 + col * 150, y = 635 + row * 145;
            pads[(size_t)i]->setBounds( x, y, 125, 115 );
            captions[cap++]->setBounds( x, y + 117, 40, 14 );
        }

        // --- Left column --------------------------------------------------------------------------
        buttonsById[DCC::EN_LINK]->setBounds( 60, 490, 80, 48 );
        buttonsById[DCC::EN_FULL_LEVEL]->setBounds( 40, 955, 75, 50 );
        captions[cap++]->setBounds( 25, 1010, 105, 14 ); // HALF LVL
        buttonsById[DCC::EN_16LEVELS]->setBounds( 40, 1060, 75, 50 );
        captions[cap++]->setBounds( 25, 1115, 105, 14 ); // NOTES
        buttonsById[DCC::EN_ERASE]->setBounds( 40, 1170, 75, 50 );

        // --- Banks --------------------------------------------------------------------------------
        for( int b = 0; b < 4; ++b )
        {
            const int x = 800 + b * 115;
            buttonsById[DCC::EN_BANK_A + b]->setBounds( x, 635, 65, 40 );
            captions[cap++]->setBounds( x, 683, 65, 14 ); // E..H
        }

        // --- Function keys row 1 -------------------------------------------------------------------
        const int fx[5] = { 795, 885, 975, 1060, 1150 };
        const int row1[5] = { DCC::EN_MAINMODE, DCC::EN_BROWSE, DCC::EN_TRACK_MIX, DCC::EN_TRACK_MUTE, DCC::EN_NEXT_SEQ };
        for( int i = 0; i < 5; ++i )
        {
            buttonsById[row1[i]]->setBounds( fx[i], 735, 70, 48 );
            captions[cap++]->setBounds( fx[i] - 12, 790, 94, 14 );
        }

        // --- Around the jog wheel -----------------------------------------------------------------
        buttonsById[DCC::EN_TAP_TEMPO]->setBounds( 795, 840, 70, 50 );
        captions[cap++]->setBounds( 783, 896, 94, 14 ); // MASTER
        buttonsById[DCC::EN_NOTE_REPEAT]->setBounds( 795, 945, 70, 50 );
        captions[cap++]->setBounds( 783, 1001, 94, 14 ); // LATCH
        buttonsById[DCC::EN_MENU]->setBounds( 1150, 840, 70, 50 );
        captions[cap++]->setBounds( 1138, 896, 94, 14 ); // PREFS
        buttonsById[DCC::EN_SHIFT]->setBounds( 1150, 940, 70, 60 );

        jog.setBounds( 935, 850, 150, 150 );
        buttonsById[DCC::EN_BACK]->setBounds( 892, 812, 48, 40 );
        buttonsById[DCC::EN_COPY]->setBounds( 1080, 812, 48, 40 );
        buttonsById[DCC::EN_MINUS]->setBounds( 892, 1000, 48, 40 );
        buttonsById[DCC::EN_PLUS]->setBounds( 1080, 1000, 48, 40 );
        buttonsById[DCC::EN_KNOB_ENTER]->setBounds( 985, 1012, 50, 28 ); // wheel push

        // --- Function keys row 2 -------------------------------------------------------------------
        const int row2[5] = { DCC::EN_STEP_SEQ, DCC::EN_TC, DCC::EN_SAMPLER, DCC::EN_SAMPLE_EDIT, DCC::EN_PROG_EDIT };
        for( int i = 0; i < 5; ++i )
        {
            buttonsById[row2[i]]->setBounds( fx[i], 1055, 70, 50 );
            captions[cap++]->setBounds( fx[i] - 12, 1112, 94, 14 );
        }

        // --- Transport ---------------------------------------------------------------------------
        const int tx[5] = { 830, 920, 1010, 1100, 1185 };
        const int tr[5] = { DCC::EN_RECORD, DCC::EN_OVERDUB, DCC::EN_STOP, DCC::EN_PLAY, DCC::EN_PLAYSTART };
        for( int i = 0; i < 5; ++i )
            buttonsById[tr[i]]->setBounds( tx[i] - 26, 1160, 52, 52 );

        // --- Printed labels and the knobs ------------------------------------------------------------
        captions[cap++]->setBounds( 30, 145, 120, 16 ); // MASTER
        master.setBounds( 42, 160, 90, 90 );
        captions[cap++]->setBounds( 38, 252, 40, 12 ); // MIN
        captions[cap++]->setBounds( 96, 252, 40, 12 ); // MAX
        captions[cap++]->setBounds( 30, 290, 120, 16 ); // REC
        rec.setBounds( 42, 305, 90, 90 );
        captions[cap++]->setBounds( 38, 397, 40, 12 ); // MIN
        captions[cap++]->setBounds( 96, 397, 40, 12 ); // MAX

        const int py[4] = { 55, 190, 310, 435 };
        for( int i = 0; i < 4; ++i )
        {
            qlinks[i].setBounds( 1105, py[i], 80, 80 );
            captions[cap++]->setBounds( 1105, py[i] + 82, 80, 14 ); // P1..P4
        }
        captions[cap++]->setBounds( 985, 606, 70, 16 ); // BANK

        slider.setBounds( 50, 645, 48, 250 );
    }

    //==============================================================================================
    ScaledPanel::ScaledPanel()
    {
        addAndMakeVisible( panel );
        setSize( Simulator::canvasW, Simulator::canvasH );
    }

    void ScaledPanel::resized()
    {
        const float s = juce::jmin( (float)getWidth() / (float)Simulator::canvasW, (float)getHeight() / (float)Simulator::canvasH );
        const float w = Simulator::canvasW * s, h = Simulator::canvasH * s;
        panel.setTransform( juce::AffineTransform::scale( s ).translated( ( (float)getWidth() - w ) * 0.5f, ( (float)getHeight() - h ) * 0.5f ) );
    }
} // namespace sim
