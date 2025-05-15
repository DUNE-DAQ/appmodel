/**
 * @file DFO.cpp
 *
 * Implementation of WIECApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "conffwk/Configuration.hpp"
#include "logging/Logging.hpp"

#include "appmodel/NWDetDataReceiver.hpp"
#include "confmodel/NetworkInterface.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/GeoId.hpp"

#include "appmodel/appmodelIssues.hpp"
#include "ConfigObjectFactory.hpp"
#include "appmodel/WIECApplication.hpp"

#include "appmodel/WIBModule.hpp"
#include "appmodel/WIBModuleConf.hpp"
#include "appmodel/WIBSettings.hpp"
#include "appmodel/HermesDataSender.hpp"
#include "appmodel/HermesModule.hpp"
#include "appmodel/HermesModuleConf.hpp"
#include "appmodel/IpbusAddressTable.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"


#include <string>
#include <vector>
#include <iostream>
#include <fmt/core.h>

namespace dunedaq {
namespace appmodel {

std::vector<const confmodel::DaqModule*> 
WIECApplication::generate_modules(conffwk::Configuration* config,
                                            const std::string& dbfile,
                                            const confmodel::Session* session) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  std::map<std::string, std::vector<const appmodel::HermesDataSender*>> ctrlhost_sender_map;


  // uint16_t conn_idx = 0;
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
    auto det_receiver = d2d_conn->get_receiver();

    // Ensure that receiver is a nw_receiver
    const auto* nw_receiver = det_receiver->cast<appmodel::NWDetDataReceiver>();

    if ( !nw_receiver ) {
      throw(BadConf(ERS_HERE, fmt::format("WEICApplication requires NWDetDataReceiver, found {} of class {}", det_receiver->UID(), det_receiver->class_name())));
    }
  
    // Loop over senders
    for (const auto* sender : det_senders) {

      if ( sender->disabled(*session) ) {
        TLOG() << "Skipping disabled sender: " << sender->UID();
        continue;
      }
      
      // Check the sender type, must me a HermesSender
      const auto* hrms_sender = sender->cast<appmodel::HermesDataSender>();
      if (!hrms_sender ) {
        throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appmodel::HermesDataSender", sender->UID())));
      }

      ctrlhost_sender_map[hrms_sender->get_control_host()].push_back(hrms_sender);
    }


    for( const auto& [ctrlhost, senders] : ctrlhost_sender_map ) {

      // Create WIBModule
      if ( this->get_wib_module_conf() ) {

        bool enable_fembs[4] = {false, false, false, false};

        for ( const auto* sender : senders ){
          for ( const auto* res : sender->get_contains() ) {
            // Loop over streams for this sender
            const auto* det_stream = res->cast<confmodel::DetectorStream>();
            // Retrieve stream_id and calculate the femb_id
            uint32_t stream_id = det_stream->get_geo_id()->get_stream_id();
            uint32_t femb_id = (stream_id & 0xf) / 2 + 2*((stream_id >> 6) & 0xf);

            // std::cout << std::format("stream {} -> femb {}", stream_id, femb_id) << std::endl;

            // Enable the femb if any of the associated streams is enabld
            // Senders in this senders list should be enabled, but better safe than sorry.
            enable_fembs[femb_id] |= !det_stream->disabled(*session);
          }
        }
        conffwk::ConfigObject wib_obj;
        std::string wib_uid = fmt::format("wib-ctrl-{}-{}", this->UID(), ctrlhost);
        config->create(dbfile, "WIBModule", wib_uid, wib_obj);
        wib_obj.set_by_val<std::string>("wib_addr", fmt::format("{}://{}:{}", this->get_wib_module_conf()->get_communication_type(), ctrlhost, this->get_wib_module_conf()->get_communication_port()));
        wib_obj.set_by_val<bool>("enabled_femb0", enable_fembs[0]);
        wib_obj.set_by_val<bool>("enabled_femb1", enable_fembs[1]);
        wib_obj.set_by_val<bool>("enabled_femb2", enable_fembs[2]);
        wib_obj.set_by_val<bool>("enabled_femb3", enable_fembs[3]);
        wib_obj.set_obj("conf", &this->get_wib_module_conf()->get_settings()->config_object());
        modules.push_back(config->get<appmodel::WIBModule>(wib_obj));
      }

      // Create Hermes Modules
      if (this->get_hermes_module_conf()) {
        conffwk::ConfigObject hermes_obj;
        std::string hermes_uid = fmt::format("hermes-ctrl-{}-{}", this->UID(), ctrlhost);
        config->create(dbfile, "HermesModule", hermes_uid, hermes_obj);
        hermes_obj.set_obj("address_table", &this->get_hermes_module_conf()->get_address_table()->config_object());
        hermes_obj.set_by_val<std::string>("uri", fmt::format("{}://{}:{}", this->get_hermes_module_conf()->get_ipbus_type(), ctrlhost, this->get_hermes_module_conf()->get_ipbus_port()));
        hermes_obj.set_by_val<uint32_t>("timeout_ms", this->get_hermes_module_conf()->get_ipbus_timeout_ms());
        hermes_obj.set_obj("destination", &nw_receiver->get_uses()->config_object());

        std::vector< const conffwk::ConfigObject * > links_obj; 
        for ( const auto* sndr : senders ){
          links_obj.push_back(&sndr->config_object());
        }
        hermes_obj.set_objs("links", links_obj);

        modules.push_back(config->get<appmodel::HermesModule>(hermes_obj));
      }


    }

  }

  return modules;
}
 
} // namespace appmodel  
} // namespace dunedaq
