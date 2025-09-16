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
#include "confmodel/NetworkInterface.hpp"

#include "ConfigObjectFactory.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "appmodel/FelixDataSender.hpp"
#include "appmodel/DaphneConf.hpp"
#include "appmodel/DaphneMap.hpp"
#include "appmodel/DaphneV2BoardConf.hpp"
#include "appmodel/DaphneV2Channel.hpp"
#include "appmodel/DaphneV2AFE.hpp"
#include "appmodel/DaphneV2ADC.hpp"
#include "appmodel/DaphneV2PGA.hpp"
#include "appmodel/DaphneV2LNA.hpp"
#include "appmodel/DaphneV2ControllerModule.hpp"
#include "appmodel/DaphneV3ControllerModule.hpp"
#include "appmodel/DaphneApplication.hpp"
#include "appmodel/FelixDetectorToDaqConnection.hpp"
#include "appmodel/NetworkDetectorToDaqConnection.hpp"
#include "appmodel/FelixDataSender.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/HermesDataSender.hpp"

#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>
#include <set>

namespace dunedaq {
namespace appmodel {
  
std::vector<const confmodel::Resource*>
DaphneApplication::contained_resources() const {
  return to_resources(get_detector_connections());
}


std::vector<const confmodel::DaqModule*> 
DaphneApplication::generate_modules(const confmodel::Session* session) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  auto daphne_conf = get_configuration();

  std::map<std::string, const DaphneV2BoardConf*> conf_map;
  auto confs = daphne_conf->get_boards();
  for ( const auto & c : confs ) {
    conf_map[c->get_ip()] = c->get_conf();
  }
 
  std::map<std::string, bool> v3_map;
  
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

    auto flx_conn = dynamic_cast<const appmodel::FelixDetectorToDaqConnection *>( d2d_conn );
    auto net_conn = dynamic_cast<const appmodel::NetworkDetectorToDaqConnection *>( d2d_conn );

    if ( ! net_conn) {
      if ( ! flx_conn ) throw BadConf(ERS_HERE, d2d_conn->UID() + " is neither felix or eth connection");
    }

    if ( flx_conn ) {
      auto det_senders = flx_conn->get_felix_senders();

      // Loop over senders
      for (const auto* felix_sender : det_senders) {
	
	if ( felix_sender->is_disabled(*session) ) {
	  TLOG() << "Skipping disabled sender: " << felix_sender->UID();
	  continue;
	}
	
	auto ip = felix_sender -> get_control_host();
	
	// from the felix sender we get the DetStream and then the GeoID
	
	auto streams = felix_sender -> get_streams();
	
	for ( const auto * det_s : streams ) {
	  
	  if ( det_s->is_disabled(*session) ) {
	    TLOG() << "Skipping disabled DetStream: " << det_s->UID();
	    continue;
	  }

	  auto geo_id = det_s->get_geo_id();
	  auto id = fmt::format("{},{},{}", geo_id->get_detector_id(), geo_id->get_crate_id(), geo_id->get_slot_id());
	  if (!v3_map.contains(id)) {
	    v3_map[id] = false;
	  } 
	  
	} // loop over DetStreams
	
      } // loop over det_senders
    } // if flx connection

    if ( net_conn ) {
      auto det_senders = net_conn->get_net_senders();

      for ( const auto* nw_sender : det_senders ) {
	if ( nw_sender->is_disabled(*session) ) {
          TLOG() << "Skipping disabled sender: " << nw_sender->UID();
          continue;
        }

	// Check the sender type, must me a HermesSender
	const auto* hrms_sender = nw_sender->cast<appmodel::HermesDataSender>();
	if (!hrms_sender ) {
	  throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appmodel::HermesDataSender", nw_sender->UID())));
	}
	
	auto streams = nw_sender -> get_streams();
	for ( const auto * det_s : streams ) {
	  
          if ( det_s->is_disabled(*session) ) {
            TLOG() << "Skipping disabled DetStream: " << det_s->UID();
            continue;
          }
	  
	  auto geo_id = det_s->get_geo_id();
	  auto id = fmt::format("{},{},{}", geo_id->get_detector_id(), geo_id->get_crate_id(), geo_id->get_slot_id());
	  if (!v3_map.contains(id)) {
	    v3_map[id] = true;
	  } 
	  
	} // loop over streams

      } // loop over NW senders
      
    } // if net_connection

  } // loop over det2DAQ Connections


  
  
  for ( const auto & [id, v3] : v3_map ) {
  
    auto conf_it = conf_map.find(id);
    if ( conf_it == conf_map.end() ) {
      throw MissingDaphne(ERS_HERE, id);
    }
    auto conf = conf_it->second;

    conffwk::ConfigObject module_obj = obj_fac.create( (v3 ?  "DaphneV3ControllerModule" : "DaphneV2ControllerModule"), fmt::format("controller-{}", ip) );
    module_obj.set_by_val<std::string>("address", ip);
    module_obj.set_obj("daphne_conf", & daphne_conf -> config_object() );
    module_obj.set_obj("board_conf", & conf -> config_object() );
    module_obj.set_by_val<uint16_t>("slot_id", geo->get_slot_id());
    module_obj.set_by_val<uint16_t>("crate_id", geo->get_crate_id());
    module_obj.set_by_val<uint16_t>("detector_id", geo->get_detector_id());

    auto module = obj_fac.get_dal<confmodel::DaqModule>(module_obj); 
    modules.push_back(module);


    // Create Hermes Modules
    if (v3) {
      std::string hermes_uid = fmt::format("hermes-ctrl-{}", this->UID(), id);
      conffwk::ConfigObject hermes_obj = obj_fac.create("HermesModule", hermes_uid);
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
    
  } // ips
    
  return modules;
}


bool
DaphneV2BoardConf::is_channel_used(size_t ch) const {

  for ( auto ch_p : get_active_channels() ) {
    if ( ch_p->get_channel_id() == ch ) {
      return true;
    }
  }

  return false;
}

const DaphneV2Channel &
DaphneV2BoardConf::get_channel(size_t ch) const {

  for ( auto ch_p : get_active_channels() ) {
    if ( ch_p->get_channel_id() == ch ) {
      return *ch_p;
    }
  }
  
  return *get_default_channel();
}

bool
DaphneV2BoardConf::is_afe_used(size_t afe) const {

  auto begin = afe*8;
  auto end   = (afe+1)*8;
  for ( size_t i = begin; i < end; ++i) {
    if( is_channel_used(i) ) return true;
  }

  return false;
}

const DaphneV2AFE &
DaphneV2BoardConf::get_afe(size_t ch) const {

  if ( ! is_afe_used(ch) ) return *get_default_afe();
  
  for ( auto afe_p : get_active_afes() ) {
    if ( afe_p->get_afe_id() == ch ) {
      return *afe_p;
    }
  }

  throw appmodel::MissingDaphne(ERS_HERE, ch);
}


uint16_t
DaphneV2ADC::get_reg4() const {

  // ADC, reg 4 has no parsing as it's all made of booleans                                                     
  std::bitset<5> reg4;                                                                                          
  // bits 0 and 2 are reserved                                                                                  
  reg4[1] = get_low_resolution();
  reg4[3] = get_output_offset_binary();
  reg4[4] = get_MSB_first();
  return reg4.to_ulong(); 
}

uint16_t
DaphneV2PGA::get_reg51() const {

  std::bitset<14> reg51(get_lpf_cut_frequency());
  reg51 <<= 1;                                                                                                  
  reg51[4] = get_integrator_disable();
  reg51[7] = true;  // clamp is always disabled and we are in low noise mode
  reg51[13] = get_gain();

  return reg51.to_ulong() ;                  
}

uint16_t
DaphneV2LNA::get_reg52() const {

  std::bitset<16> reg52;                                                                                        

  decltype(reg52) clamp(get_clamp());
  clamp <<= 6;

  reg52[12] = get_integrator_disable();
  
  decltype(reg52) gain(get_gain());
  clamp <<= 13;

  reg52 |= clamp;
  reg52 |= gain;

  return reg52.to_ulong();
}
 
} // namespace appmodel  
} // namespace dunedaq
