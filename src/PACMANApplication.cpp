/**
 * @file DaphneApplication.cpp
 *
 * Implementation of DaphneApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "confmodel/GeoId.hpp"
#include "confmodel/DetectorStream.hpp"

#include "ConfigObjectFactory.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "appmodel/PACMANApplication.hpp"
#include "appmodel/PACMANDataSender.hpp"
#include "appmodel/PACMANDetectorToDaqConnection.hpp"
#include "appmodel/PACMANReaderModule.hpp"
#include "appmodel/PACMANConfiguration.hpp"


#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>
#include <set>

namespace dunedaq {
namespace appmodel {
  
std::vector<const confmodel::Resource*>
PACMANApplication::contained_resources() const {
  return to_resources(get_detector_connections());
}


void
PACMANApplication::generate_modules(const confmodel::Session* session) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  std::vector<const confmodel::GeoId*> geo_ids;
  
  for (auto d2d_conn : get_detector_connections()) {

    // A Resource can be disabled and still its application can be enabled because the application can have multile resources, so we need to check which resources are enabled
    if (d2d_conn->is_disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn->UID();
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module

    // Redundant? Schema forbids 0 connections
    if (d2d_conn->contained_resources().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain senders or receivers"));
    }

    auto det_senders = d2d_conn->get_pacman_senders();

    // Loop over senders
    for (const auto* pacman_sender : det_senders) {

      if ( pacman_sender->is_disabled(*session) ) {
        TLOG() << "Skipping disabled sender: " << pacman_sender->UID();
        continue;
      }

      auto streams = pacman_sender -> get_streams();

      for ( const auto * det_s : streams ) {

        if ( det_s->is_disabled(*session) ) {
            TLOG() << "Skipping disabled DetStream: " << det_s->UID();
            continue;
        }
        geo_ids.push_back(det_s->get_geo_id());        

      } // loop over DetStreams
      
    } // loop over det_senders

  } // loop over det2DAQ Connections

  for ( const auto & geo : geo_ids ) {
  
    auto slot = geo->get_slot_id();
    conffwk::ConfigObject module_obj = obj_fac.create("PACMANReaderModule", fmt::format("controller-{}", slot));
    auto module = obj_fac.get_dal<appmodel::PACMANReaderModule>(module_obj);
    modules.push_back(module);
    
  } // ips

  obj_fac.update_modules(modules);
}

} // namespace appmodel  
} // namespace dunedaq
