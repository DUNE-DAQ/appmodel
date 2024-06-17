/**
 * @file DFO.cpp
 *
 * Implementation of WIECApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "appmodel/WIECApplication.hpp"


#include <string>
#include <vector>
#include <iostream>
#include <fmt/core.h>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("WIECApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* confdb,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<WIECApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
WIECApplication::generate_modules(conffwk::Configuration* confdb,
                                            const std::string& dbfile,
                                            const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;

  std::map<std::string, std::vector<const appmodel::HermesDataSender*>> ctrlhost_sender_map;


  uint16_t conn_idx = 0;
  for (auto d2d_conn_res : get_contains()) {

    // Are we sure?
    if (d2d_conn_res->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
      continue;
    }

    d2d_conn_objs.push_back(&d2d_conn_res->config_object());

    TLOG() << "Processing DetectorToDaqConnection " << d2d_conn_res->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
    auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

    if (!d2d_conn) {
      throw(BadConf(ERS_HERE, "ReadoutApplication contains something other than DetectorToDaqConnection"));
    }

    if (d2d_conn->get_contains().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain sebders or receivers"));
    }

    // Loop over detector 2 daq connections to find senders and receivers
    auto det_senders = d2d_conn->get_senders();
    auto det_receiver = d2d_conn->get_receiver();

  
    // Loop over senders
    for (const auto* sender : det_senders) {

      const auto* hrms_sender = sender->cast<appdal::HermesDataSender>();
      if (!hrms_sender ) {
        throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appdal::HermesDataSender",sender->UID())));
      }

      // Are we sure?
      if (hrms_sender->disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorStream " << stream->UID();
        continue;
      }

      ctrlhost_sender_map[hrms_sender->get_control_host()].push_back(hrms_sender);
    }


    for( const [ctrlhost, senders]& : ctrlhost_sender_map ) {
      fmt::print(">> {} len {}\n", ctrlhost, senders.size());


    }

  }

  return modules;
}
