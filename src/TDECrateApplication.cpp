/**
 * @file TDECrateApplication.cpp
 *
 * Implementation of TDECrateApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "appmodel/NWDetDataReceiver.hpp"
#include "confmodel/NetworkInterface.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"

#include "appmodel/appmodelIssues.hpp"
#include "ConfigObjectFactory.hpp"
#include "appmodel/TDECrateApplication.hpp"
#include "appmodel/TdeAmcDetDataSender.hpp"
#include "appmodel/TDEAMCModule.hpp"
#include "appmodel/TDEAMCModuleConf.hpp"

#include <string>
#include <vector>
#include <iostream>
#include <fmt/core.h>

namespace dunedaq {
namespace appmodel {

std::vector<const confmodel::DaqModule*> 
TDECrateApplication::generate_modules(const confmodel::Session* session) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  std::map<std::string, std::vector<const appmodel::TdeAmcDetDataSender*>> ctrlhost_sender_map;

  for (auto d2d_conn_res : get_contains()) {
        // Are we sure?
    if (d2d_conn_res->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn_res->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
    auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

    if (!d2d_conn) {
      throw(BadConf(ERS_HERE, "ReadoutApplication contains something other than DetectorToDaqConnection"));
    }

    if (d2d_conn->get_contains().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain senders or receivers"));
    }
    auto det_senders = d2d_conn->get_senders();


    // Loop over senders
    for (const auto* sender : det_senders) {

      if ( sender->disabled(*session) ) {
        TLOG() << "Skipping disabled sender: " << sender->UID();
        continue;
      }
      
      // Check the sender type, must me a TdeAmcDetDataSender
      const auto* tde_sender = sender->cast<appmodel::TdeAmcDetDataSender>();
      if (!tde_sender ) {
        throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appmodel::TdeAmcDetDataSender", sender->UID())));
      }

      ctrlhost_sender_map[tde_sender->get_control_host()].push_back(tde_sender);
    }

    for( const auto& [ctrlhost, senders] : ctrlhost_sender_map ) {
      if ( this->get_tde_amc_module_conf() ) {
        conffwk::ConfigObject tde_obj = obj_fac.create( "TDEAMCModule", fmt::format("tde-ctrl-{}-{}", this->UID(), ctrlhost));
        // std::string tde_uid = fmt::format("tde-ctrl-{}-{}", this->UID(), ctrlhost);
        // config->create(dbfile, "TDEAMCModule", tde_uid, tde_obj);
        tde_obj.set_obj("amc", &(senders[0]->config_object()) ); // for now just allow one AMC per module
        modules.push_back(obj_fac.get_dal<appmodel::TDEAMCModule>(tde_obj));
      }
    }
  }
  return modules;
}

} // namespace appmodel  
} // namespace dunedaq
