/**
 * @file DaphneApplication.cpp
 *
 * Implementation of DaphneApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ConfigObjectFactory.hpp"
#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/DetDataSender.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/DetectorStream.hpp"

#include "appmodel/appmodelIssues.hpp"
#include "appmodel/FelixDataSender.hpp"
#include "appmodel/DaphneConf.hpp"
#include "appmodel/DaphneV2BoardConf.hpp"
#include "appmodel/DaphneV2Channel.hpp"
#include "appmodel/DaphneV2AFE.hpp"
#include "appmodel/DaphneV2ADC.hpp"
#include "appmodel/DaphneV2PGA.hpp"
#include "appmodel/DaphneV2LNA.hpp"
#include "appmodel/DaphneV2ControllerModule.hpp"
#include "appmodel/DaphneApplication.hpp"


#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>
#include <set>

namespace dunedaq {
namespace appmodel {
  
std::vector<const confmodel::DaqModule*> 
DaphneApplication::generate_modules(conffwk::Configuration* config,
                                    const std::string& dbfile,
                                    const confmodel::Session* session) const
{
  const auto obj_fac = ConfigObjectFactory(this);

  std::vector<const confmodel::DaqModule*> modules;

  auto daphne_conf = get_configuration();

  std::map<std::string, const confmodel::GeoId*> geo_ids;
  
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
        continue;
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

      // from the felix sender we get the DetStream and then the GeoID

      auto streams = felix_sender -> get_contains();

      for ( const auto * det_s : streams ) {

	if ( det_s->disabled(*session) ) {
	  TLOG() << "Skipping disabled DetStream: " << det_s->UID();
	  continue;
	}

	if (!geo_ids.contains(ip)) {
	  const auto * temp_stream = det_s->cast<confmodel::DetectorStream>();
	  geo_ids[ip] = temp_stream->get_geo_id();
	} 
	 
      } // loop over DetStreams
      
    } // loop over det_senders

  } // loop over det2DAQ Connections

  for ( const auto & [ip, geo] : geo_ids ) {
  
    auto slot = geo->get_slot_id();

    const auto raw_conf = daphne_conf->get_json().at(ip);

    // setup channels
    std::vector<const conffwk::ConfigObject*> channels;
    const auto raw_channels = raw_conf["channel_analog_conf"];
    const auto raw_ids = raw_channels["ids"].get<std::vector<uint8_t>>();
    const auto raw_gains = raw_channels["gains"].get<std::vector<uint8_t>>();
    const auto raw_offsets = raw_channels["offsets"].get<std::vector<uint16_t>>();
    const auto raw_trims = raw_channels["trims"].get<std::vector<uint16_t>>();
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
    const auto raw_afes = raw_conf["afes"];
    const auto raw_afe_ids = raw_afes["ids"].get<std::vector<size_t>>();
    const auto raw_afe_attenuators = raw_afes["attenuators"].get<std::vector<uint16_t>>();
    const auto raw_afe_biases = raw_afes["v_biases"].get<std::vector<uint16_t>>();
    const auto raw_adcs = raw_afes["adcs"];
    const auto raw_adc_res = raw_adcs["resolution"].get<std::vector<uint16_t>>();
    const auto raw_adc_format = raw_adcs["output_format"].get<std::vector<uint16_t>>();
    const auto raw_adc_SB = raw_adcs["SB_first"].get<std::vector<uint16_t>>();
    const auto raw_lnas = raw_afes["lnas"];
    const auto raw_lna_clamps = raw_lnas["clamp"].get<std::vector<uint8_t>>();
    const auto raw_lna_gains = raw_lnas["gain"].get<std::vector<uint8_t>>();
    const auto raw_lna_integrators = raw_lnas["integrator_disable"].get<std::vector<uint16_t>>();
    const auto raw_pgas = raw_afes["pgas"];
    const auto raw_pga_cuts = raw_pgas["lpf_cut_frequency"].get<std::vector<uint8_t>>();
    const auto raw_pga_integrators = raw_pgas["integrator_disable"].get<std::vector<uint16_t>>();
    const auto raw_pga_gains = raw_pgas["gain"].get<std::vector<uint16_t>>();
    for ( size_t i = 0; i < raw_afe_ids.size(); ++i ) {
      auto id = raw_afe_ids[i];
      
      // create the adc
      conffwk::ConfigObject adc_obj;
      config->create(dbfile, "DaphneV2ADC",
                     fmt::format("daphne-{}-adc-{}", slot, id), adc_obj);
      adc_obj.set_by_val<bool>("low_resolution",  raw_adc_res[i] > 0);
      adc_obj.set_by_val<bool>("output_offset_binary",  raw_adc_format[i] > 0 );
      adc_obj.set_by_val<bool>("MSB_first",  raw_adc_SB[i] > 0);
      auto adc = config->get<appmodel::DaphneV2ADC>(adc_obj);
      
      // create the lna
      conffwk::ConfigObject lna_obj;
      config->create(dbfile, "DaphneV2LNA",
                     fmt::format("daphne-{}-lna-{}", slot, id), lna_obj);
      lna_obj.set_by_val<uint8_t>("clamp",  raw_lna_clamps[i]);
      lna_obj.set_by_val<uint8_t>("gain",  raw_lna_gains[i]);
      lna_obj.set_by_val<bool>("integrator_disable",  raw_lna_integrators[i]>0);
      auto lna = config->get<appmodel::DaphneV2LNA>(lna_obj);
      
      // create the pga
      conffwk::ConfigObject pga_obj;
      config->create(dbfile, "DaphneV2PGA",
                     fmt::format("daphne-{}-pga-{}", slot, id), pga_obj);
      pga_obj.set_by_val<uint8_t>("lpf_cut_frequency",  raw_pga_cuts[i]);
      pga_obj.set_by_val<bool>("gain",  raw_pga_gains[i]>0);
      pga_obj.set_by_val<bool>("integrator_disable",  raw_pga_integrators[i]>0);
      auto pga = config->get<appmodel::DaphneV2PGA>(pga_obj);
      
      // finally create the afe
      conffwk::ConfigObject afe_obj;
      config->create(dbfile, "DaphneV2AFE",
                     fmt::format("daphne-{}-afe-{}", slot, id),afe_obj );
      afe_obj.set_by_val<uint8_t>("afe_id", id);
      afe_obj.set_by_val<uint16_t>("attenuator", raw_afe_attenuators[i]);
      afe_obj.set_by_val<uint16_t>("v_bias", raw_afe_biases[i]);
      afe_obj.set_obj("adc", & adc -> config_object() );
      afe_obj.set_obj("lna", & lna -> config_object() );
      afe_obj.set_obj("pga", & pga -> config_object() );
      auto afe = config->get<appmodel::DaphneV2AFE>(afe_obj);
      afes.push_back( & afe-> config_object());
    }
    
    
    conffwk::ConfigObject board_obj;
    config->create(dbfile, "DaphneV2BoardConf",
                   fmt::format("daphne-{}-conf", slot), board_obj);
    board_obj.set_by_val<uint16_t>("bias_ctrl", raw_conf.at("bias_ctrl"));
    board_obj.set_by_val<uint64_t>("self_trigger_threshold", raw_conf.at("self_trigger_threshold"));
    board_obj.set_by_val<std::vector<uint8_t>>("full_stream_channels",
                                               raw_conf.at("full_stream_channels").get<std::vector<uint8_t>>());
    board_obj.set_by_val<uint64_t>("self_trigger_xcorr", raw_conf.at("self_trigger_xcorr"));
    board_obj.set_by_val<uint32_t>("tp_conf", raw_conf.at("tp_conf"));
    board_obj.set_by_val<uint64_t>("compensator", raw_conf.at("compensator"));
    board_obj.set_by_val<uint64_t>("inverter", raw_conf.at("inverter"));
    board_obj.set_by_val<uint16_t>("slot_id", geo->get_slot_id());
    board_obj.set_by_val<uint16_t>("crate_id", geo->get_crate_id());
    board_obj.set_by_val<uint16_t>("detector_id", geo->get_detector_id());
    board_obj.set_objs("active_channels", channels);
    board_obj.set_objs("active_afes", afes);
    board_obj.set_obj("default_channel", & daphne_conf->get_default_v2_settings()->get_default_channel()->config_object());
    board_obj.set_obj("default_afe", & daphne_conf->get_default_v2_settings()->get_default_afe()->config_object());
    auto conf = config->get<appmodel::DaphneV2BoardConf>(board_obj);
    
    conffwk::ConfigObject module_obj;
    std::string module_name = fmt::format("controller-{}", slot);
    config -> create( dbfile, "DaphneV2ControllerModule", module_name, module_obj);
    module_obj.set_by_val<std::string>("address", ip);
    module_obj.set_obj("daphne_conf", & daphne_conf -> config_object() );
    module_obj.set_obj("board_conf", & conf -> config_object() );
    
    auto module = config->get<appmodel::DaphneV2ControllerModule>(module_obj);
    modules.push_back(module);
    
  } // ips

  return modules;
}


uint16_t DaphneConf::get_board_slot(const std::string & ip) const {

  auto conf_dict = get_json();
  auto it = conf_dict.find(ip);
  if ( it == conf_dict.end() ) {
    throw MissingIP(ERS_HERE, ip);
  }

  return it->at("slot").get<uint16_t>();
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
