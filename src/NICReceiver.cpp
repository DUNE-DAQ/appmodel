/**
 * @file NICReceiver.cpp
 *
 * Implementation of NICReceiver's dal methods
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appdal/DROStreamConf.hpp"
#include "appdal/EthStreamParameters.hpp"
#include "appdal/NICReceiver.hpp"

#include "appdalIssues.hpp"

using namespace dunedaq::appdal;

std::string NICReceiver::get_ipaddress() {
  auto streams = get_streams();
  if (streams.size() == 0) {
    return "";
  }
  auto streamPars = streams[0]->get_stream_params();
  auto ethPars = streamPars->cast<EthStreamParameters>();
  if (streamPars == nullptr) {
    throw (BadStreamConf(ERS_HERE, streamPars->UID(), "EthStreamParameters"));
  }
  return ethPars->get_rx_ip();
}

std::string NICReceiver::get_macaddress() {
  auto streams = get_streams();
  if (streams.size() == 0) {
    return "";
  }
  auto streamPars = streams[0]->get_stream_params();
  auto ethPars = streamPars->cast<EthStreamParameters>();
  if (streamPars == nullptr) {
    throw (BadStreamConf(ERS_HERE, streamPars->UID(), "EthStreamParameters"));
  }
  return ethPars->get_rx_mac();
}
