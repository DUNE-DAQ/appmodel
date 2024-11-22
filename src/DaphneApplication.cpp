/**
 * @file DaphneApplication.cpp
 *
 * Implementation of DaphneApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/DetDataSender.hpp"

#include "appmodel/FelixDataSender.hpp"
#include "appmodel/DaphneConf.hpp"
#include "appmodel/DaphneV2BoardConf.hpp"
#include "appmodel/DaphneV2Channel.hpp"
#include "appmodel/DaphneV2AFE.hpp"
#include "appmodel/DaphneV2ADC.hpp"
#include "appmodel/DaphneV2ControllerModule.hpp"
#include "appmodel/DaphneApplication.hpp"


#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("DaphneApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* config,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<DaphneApplication>();
    return app->generate_modules(config, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
DaphneApplication::generate_modules(conffwk::Configuration* config,
				    const std::string& dbfile,
				    const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  auto daphne_conf = get_configuration();
  
  for (auto d2d_conn_res : get_contains()) {

    // A Resource can be disabled and still its application can be enabled because the application can have multile resources, so we need to check which resources are enabled
    if (d2d_conn_res->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn_res->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
    auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

    if (!d2d_conn) {
      throw(BadConf(ERS_HERE, "DaphneApplication contains something other than DetectorToDaqConnection"));
    }

    if (d2d_conn->get_contains().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain senders or receivers"));
    }

    auto det_senders = d2d_conn->get_senders();

    // Loop over senders
    for (const auto* sender : det_senders) {

      if ( sender->disabled(*session) ) {
	TLOG() << "Skipping disabled sender: " << sender->UID();
      }
      // Check the sender type, must me a FelixDataSender
      const auto* felix_sender = sender->cast<appmodel::FelixDataSender>();
      if (!felix_sender ) {
	//throw(BadConf(ERS_HERE, fmt::format("DataSender {} is not a appmodel::HermesDataSender", sender->UID())));
	continue;
	// MaR: I don't think we should throw here because there can be other connections other than felix
	// MaR: should we be worried that we assume that a Felix connection is a Daphne?
      }

      auto ip = felix_sender -> get_control_host();
      auto slot = daphne_conf -> get_board_slot(ip);

      const auto raw_conf = daphne_conf->get_configuration().at(ip);

      // setup channels
      std::vector<const conffwk::ConfigObject*> channels;
      auto raw_channels = raw_conf["channel_analog_conf"];
      auto raw_ids = raw_channels["ids"].get<std::vector<uint8_t>>();
      auto raw_gains = raw_channels["gains"].get<std::vector<uint8_t>>();
      auto raw_offsets = raw_channels["offsets"].get<std::vector<uint16_t>>();
      auto raw_trims = raw_channels["trims"].get<std::vector<uint16_t>>();
      for ( size_t i = 0; i < raw_ids.size(); ++i ) {
	auto id = raw_ids[i];
	conffwk::ConfigObject channel_obj;
	config->create(dbfile, "DaphneV2Channel",
		       fmt::format("daphne-{}-channel-{}", slot, id), channel_obj );
	channel_obj.set_by_val<uint8_t>("channel_id", id);
	channel_obj.set_by_val<uint8_t>("gain", raw_gains[i]);
	channel_obj.set_by_val<uint16_t>("offset", raw_offsets[i]);
	channel_obj.set_by_val<uint16_t>("trim", raw_trims[i]);
	auto ch = config->get<appmodel::DaphneV2Channel>(channel_obj);
	channels.push_back(& ch -> config_object());
      }

      //setup afes
      std::vector<const conffwk::ConfigObject*> afes;
      // auto raw_afes = raw_conf["afes"];
      // auto raw_biases = raw_afes["v_biases"];
      // for ( auto bias : raw_biases ) {
	
      // }

      
      conffwk::ConfigObject board_obj;
      config->create(dbfile, "DaphneV2BoardConf",
       		     fmt::format("daphne-{}-conf", slot), board_obj);
      board_obj.set_by_val<uint16_t>("bias_ctrl", raw_conf.at("bias_ctrl"));
      board_obj.set_by_val<uint64_t>("self_trigger_threshold", raw_conf.at("self_trigger_threshold"));
      board_obj.set_objs("active_channels", channels);
      board_obj.set_objs("active_afes", afes);
      auto conf = config->get<appmodel::DaphneV2BoardConf>(board_obj);
      
      conffwk::ConfigObject module_obj;
      std::string module_name = fmt::format("controller-{}", slot);
      config -> create( dbfile, "DaphneV2ControllerModule", module_name, module_obj);
      module_obj.set_by_val<std::string>("address", ip);
      module_obj.set_by_val<uint16_t>("slot", slot);
      module_obj.set_obj("daphne_conf", & daphne_conf -> config_object() );
      module_obj.set_obj("board_conf", & conf -> config_object() );

      auto module = config->get<appmodel::DaphneV2ControllerModule>(module_obj);
      modules.push_back(module);
      
    } // loop over data senders

  }  // loop over detector 2 daq connections

  return modules;
}


uint16_t DaphneConf::get_board_slot(const std::string & ip) const {

  auto conf_dict = get_configuration();
  auto it = conf_dict.find(ip);
  if ( it == conf_dict.end() ) {
    throw MissingIP(ERS_HERE, ip);
  }

  return it->at("slot").get<uint16_t>();
 
  return 0;
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

const DaphneV2AFE &
DaphneV2BoardConf::get_afe(size_t ch) const {

  for ( auto afe_p : get_active_afes() ) {
    if ( afe_p->get_afe_id() == ch ) {
      return *afe_p;
    }
  }
  
  return *get_default_afe();
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


