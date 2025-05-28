/**
 * @file NetworkDetectorToDaqConnection.cpp
 *
 * Implementation of NetworkDetectorToDaqConnection methods
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/NetworkDetectorToDaqConnection.hpp"
#include "appmodel/NWDetDataReceiver.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/DetDataSender.hpp"

namespace dunedaq::appmodel {

std::vector<const dunedaq::confmodel::DetDataSender*> 
NetworkDetectorToDaqConnection::get_senders() const {
  std::vector<const dunedaq::confmodel::DetDataSender*> senders;
  for (auto sender: m_net_senders) {
    senders.push_back(
      dynamic_cast<const dunedaq::confmodel::DetDataSender*>(sender));
  }
  return senders;
}

const confmodel::DetDataReceiver*
NetworkDetectorToDaqConnection::get_receiver() const {
  return (m_net_receiver);
}

} // namespace dunedaq::appmodel
