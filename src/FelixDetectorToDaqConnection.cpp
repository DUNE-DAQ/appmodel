/**
 * @file FelixDetectorToDaqConnection.cpp
 *
 * Implementation of FelixDetectorToDaqConnection methods
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/FelixDetectorToDaqConnection.hpp"
#include "appmodel/FelixDataReceiver.hpp"
#include "appmodel/FelixDataSender.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/DetDataSender.hpp"

namespace dunedaq::appmodel {

std::vector<const dunedaq::confmodel::DetDataSender*> 
FelixDetectorToDaqConnection::get_senders() const {
  std::vector<const dunedaq::confmodel::DetDataSender*> senders;
  for (auto sender: m_felix_senders) {
    senders.push_back(
      dynamic_cast<const dunedaq::confmodel::DetDataSender*>(sender));
  }
  return senders;
}

const confmodel::DetDataReceiver*
FelixDetectorToDaqConnection::get_receiver() const {
  return (m_felix_receiver);
}

} // namespace dunedaq::appmodel
