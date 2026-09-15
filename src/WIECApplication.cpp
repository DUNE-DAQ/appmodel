/**
 * @file DFO.cpp
 *
 * Implementation of WIECApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"
#include "conffwk/Configuration.hpp"
#include "logging/Logging.hpp"

#include "appmodel/NWDetDataReceiver.hpp"
#include "confmodel/NetworkInterface.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/GeoId.hpp"

#include "appmodel/appmodelIssues.hpp"
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

//-----------------------------------------------------------------------------

std::vector<const confmodel::ExcludableEntity*>
WIECApplication::contained_excludable_entities() const {
  return to_resources(get_detector_connections());
}


void
WIECApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{
  ConfigObjectFactory obj_fac(this);
  conffwk::Configuration* config = &this->configuration();
  const std::string& dbfile = this->config_object().contained_in();
  
  std::vector<const confmodel::DaqModule*> modules;

  std::map<std::string, std::vector<const appmodel::HermesDataSender*>> ctrlhost_sender_map;


  // uint16_t conn_idx = 0;
  for (auto d2d_conn : get_detector_connections()) {

    // Are we sure?
    if (helper->is_excluded(d2d_conn)) {
      TLOG_DEBUG(7) << "Ignoring excluded DetectorToDaqConnection " << d2d_conn->UID();
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn->UID();

    // Is this check necessary?
    if (d2d_conn->contained_excludable_entities().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain senders or receivers"));
    }

    auto det_senders = d2d_conn->senders();
    auto det_receiver = d2d_conn->receiver();

    // Ensure that receiver is a nw_receiver
    const auto* nw_receiver = det_receiver->cast<appmodel::NWDetDataReceiver>();

    if ( !nw_receiver ) {
      throw(BadConf(ERS_HERE, fmt::format("WEICApplication requires NWDetDataReceiver, found {} of class {}", det_receiver->UID(), det_receiver->class_name())));
    }
  
    // Note on how to exclude the senders.
    // The hardware interface requires that all the physical links be configured, even if not used for a run.
    // Consequently, at the configuration level, we should never remove senders from a detector to daq connection.
    // At most, we should exclude them at the level of the session.
    // For the same reason, in this code, we create a map of control hosts to senders, without checking if the senders are excluded.
    // Once the map is created, if all the senders associated to a control hosts are excluded, then we skip the creation of all related modules.
    
    // Loop over senders to create the map of control hosts to senders. 
    for (const auto* sender : det_senders) {

      // Check the sender type, must be a HermesSender
      const auto* hrms_sender = sender->cast<appmodel::HermesDataSender>();
      if (!hrms_sender ) {
        throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appmodel::HermesDataSender", sender->UID())));
      }

      ctrlhost_sender_map[hrms_sender->get_control_host()].push_back(hrms_sender);
    }


    for( const auto& [ctrlhost, senders] : ctrlhost_sender_map ) {

      // If all senders for this control host are excluded, skip creating related modules.
      // Of course the opposite logic is faster: check if any sender is included.
      bool any_included = false;
      for ( const auto* sender : senders ){
        if ( helper->is_included(sender) ) {
          any_included = true;
          break;
        }
      }
      if (!any_included) {
        TLOG_DEBUG(6) << "Skipping control host " << ctrlhost << " whose senders are all excluded.";
        continue;
      }

      // Create WIBModule
      if ( this->get_wib_module_conf() ) {

        bool enable_fembs[4] = {false, false, false, false};

        for ( const auto* sender : senders ){
          for ( const auto* det_stream : sender->get_streams() ) {
            // Loop over streams for this sender
            // Retrieve stream_id and calculate the femb_id
            uint32_t stream_id = det_stream->get_geo_id()->get_stream_id();
            uint32_t femb_id = (stream_id & 0xf) / 2 + 2*((stream_id >> 6) & 0xf);

            // std::cout << std::format("stream {} -> femb {}", stream_id, femb_id) << std::endl;

            // Enable the femb if any of the associated streams is enabled
            enable_fembs[femb_id] |= helper->is_included(det_stream);
          } // loop over streams
        } // loop over senders for this control host

        std::string wib_uid = fmt::format("wib-ctrl-{}-{}", this->UID(), ctrlhost);
        conffwk::ConfigObject wib_obj = obj_fac.create("WIBModule", wib_uid);
        wib_obj.set_by_val<std::string>("wib_addr", fmt::format("{}://{}:{}", this->get_wib_module_conf()->get_communication_type(), ctrlhost, this->get_wib_module_conf()->get_communication_port()));
        for (int i=0; i<4; ++i) {
          wib_obj.set_by_val<bool>(fmt::format("enabled_femb{}", i), enable_fembs[i]);
        }
        wib_obj.set_obj("conf", &this->get_wib_module_conf()->get_settings()->config_object());
        modules.push_back(config->get<appmodel::WIBModule>(wib_obj));
      }  // if we have a module configuration for the WIB module

      // Create Hermes Modules
      if (this->get_hermes_module_conf()) {
        std::string hermes_uid = fmt::format("hermes-ctrl-{}-{}", this->UID(), ctrlhost);
        conffwk::ConfigObject hermes_obj = obj_fac.create("HermesModule", hermes_uid);
        hermes_obj.set_obj("address_table", &this->get_hermes_module_conf()->get_address_table()->config_object());
        hermes_obj.set_by_val<std::string>("uri", fmt::format("{}://{}:{}", this->get_hermes_module_conf()->get_ipbus_type(), ctrlhost, this->get_hermes_module_conf()->get_ipbus_port()));
        hermes_obj.set_by_val<uint32_t>("timeout_ms", this->get_hermes_module_conf()->get_ipbus_timeout_ms());
        hermes_obj.set_obj("destination", &nw_receiver->get_uses()->config_object());

        std::vector< const conffwk::ConfigObject * > links_obj; 
        for ( const auto* sndr : senders ){
          // Note that it is OK that some of these senders might be excluded
          // The hardware interface in HermesModule requires that all links be configured, even if not used for a run
          links_obj.push_back(&sndr->config_object());
        }
        hermes_obj.set_objs("links", links_obj);

        modules.push_back(config->get<appmodel::HermesModule>(hermes_obj));
      } // If we have a module configuration for the Hermes module

    } // loop over control hosts

  } // loop over detector to daq connections

  obj_fac.update_modules(modules);
}
 
} // namespace appmodel  
} // namespace dunedaq
