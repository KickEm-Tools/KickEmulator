/*
 * Copyright (c) 2022 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "kick_SimulatorInterfaceImpl.h"

namespace sim
{
    SimulatorHub& SimulatorHub::get()
    {
        static SimulatorHub hub;
        return hub;
    }

    void SimulatorHub::sendToApp( CPktMgr::HWMsg_t msg )
    {
        for( auto* impl : impls )
            impl->hwInputReceived( msg );
    }

    void SimulatorHub::attach( SimulatorInterface* impl )
    {
        if( std::find( impls.begin(), impls.end(), impl ) == impls.end() )
            impls.push_back( impl );
    }

    void SimulatorHub::detach( SimulatorInterface* impl )
    {
        impls.erase( std::remove( impls.begin(), impls.end(), impl ), impls.end() );
    }

    SimulatorInterfaceImpl::SimulatorInterfaceImpl()
    {
        SimulatorHub::get().attach( this );
    }

    SimulatorInterfaceImpl::~SimulatorInterfaceImpl()
    {
        SimulatorHub::get().detach( this );
    }

    void SimulatorInterfaceImpl::sendMessageToHW( CPktMgr::HWMsg_t& msg )
    {
        if( juce::MessageManager::getInstance()->isThisTheMessageThread() )
        {
            SimulatorHub::get().fromApp( msg );
        }
        else
        {
            auto copy = msg;
            juce::MessageManager::callAsync( [copy]() mutable { SimulatorHub::get().fromApp( copy ); } );
        }
    }

    // Registers this implementation with the Factory used by modules/input_events (SIM_BUILD).
    REGISER_FACTORY( SimulatorInterface, SimulatorInterfaceImpl, 0 );
} // namespace sim
