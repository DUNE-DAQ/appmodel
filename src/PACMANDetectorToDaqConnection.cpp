/**
 * @file PACMANDetectorToDaqConnection.cpp
 *
 * Implementation of PACMANDetectorToDaqConnection methods
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/PACMANDetectorToDaqConnection.hpp"
#include "appmodel/PACMANDataReceiver.hpp"
#include "appmodel/PACMANDataSender.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/DetDataSender.hpp"

namespace dunedaq::appmodel
{

    std::vector<const dunedaq::confmodel::DetDataSender *>
    PACMANDetectorToDaqConnection::senders() const
    {
        std::vector<const dunedaq::confmodel::DetDataSender *> senders;
        if (m_pacman_senders.empty())
        {
            std::lock_guard scoped_lock(m_mutex);
            check_init();
        }
        for (auto sender : m_pacman_senders)
        {
            senders.push_back(
                dynamic_cast<const dunedaq::confmodel::DetDataSender *>(sender));
        }
        return senders;
    }

    const confmodel::DetDataReceiver *
    PACMANDetectorToDaqConnection::receiver() const
    {
        if (m_pacman_senders.empty())
        {
            std::lock_guard scoped_lock(m_mutex);
            check_init();
        }
        return (m_pacman_receiver);
    }

} // namespace dunedaq::appmodel
