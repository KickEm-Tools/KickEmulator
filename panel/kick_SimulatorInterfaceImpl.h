/*
 * Copyright (c) 2022 MUSIC Tribe
 *
 * This file is part of Kick.
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#pragma once

#include <common/common.h>
#include <input_events/kick_SimulatorInterface.h>

namespace sim
{
    /// Single meeting point between the simulator window and the application's InputEvents.
    ///
    ///   window  --sendToApp()-->  every registered SimulatorInterface  -->  InputEvents::dispatchHardwareEvent
    ///   window  <--fromApp-----   SimulatorInterfaceImpl::sendMessageToHW  <--  InputEvents::sendMessageToHardware
    class SimulatorHub
    {
    public:
        static SimulatorHub& get();

        /// Application -> simulated surface. Always emitted on the message thread.
        sigslot::signal<CPktMgr::HWMsg_t&> fromApp;

        /// Simulated surface -> application.
        void sendToApp( CPktMgr::HWMsg_t msg );

        void attach( SimulatorInterface* impl );
        void detach( SimulatorInterface* impl );

    private:
        std::vector<SimulatorInterface*> impls;
    };

    /// The implementation input_events creates through Factory<SimulatorInterface>.
    class SimulatorInterfaceImpl : public SimulatorInterface
    {
    public:
        SimulatorInterfaceImpl();
        ~SimulatorInterfaceImpl() override;

        void sendMessageToHW( CPktMgr::HWMsg_t& msg ) override;
    };
} // namespace sim
