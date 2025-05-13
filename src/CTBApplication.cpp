/**
 * @file CTBApplication.cpp
 *
 * Implementation of CTBApplication's generate_modules dal method
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

#include "appmodel/CTBApplication.hpp"
#include "appmodel/CTBoardConf.hpp"
#include "appmodel/CTBConf.hpp"
#include "appmodel/CTBModule.hpp"
#include "appmodel/CTBSockets.hpp"
#include "appmodel/CTBTrigger.hpp"
#include "appmodel/CTBMisc.hpp"
#include "appmodel/CTBRandomTrigger.hpp"
#include "appmodel/CTBPulser.hpp"
#include "appmodel/CTBTiming.hpp"
#include "appmodel/CTBHLT.hpp"
#include "appmodel/CTBLLT.hpp"
#include "appmodel/CTBCountLLT.hpp"
#include "appmodel/CTBSubsystem.hpp"
#include "appmodel/CTBCRTSubsystem.hpp"
#include "appmodel/CTBPDSSubsystem.hpp"

#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataHandlerModule.hpp"

#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>
#include <set>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("CTBApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* config,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<CTBApplication>();
    return app->generate_modules(config, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
CTBApplication::generate_modules(conffwk::Configuration* config,
				 const std::string& dbfile,
				 const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  auto dlhConf = get_link_handler();
  auto dlhClass = dlhConf->get_template_for();

  const QueueDescriptor* dlhInputQDesc = nullptr;
    
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "DataHandlerModule" || destination_class == dlhClass) {
      dlhInputQDesc = rule->get_descriptor();
    }
  }
  
  const NetworkConnectionDescriptor* dlhReqInputNetDesc = nullptr;
  const NetworkConnectionDescriptor* tsNetDesc = nullptr;
  const NetworkConnectionDescriptor* hsiNetDesc = nullptr;
  
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    
    if (endpoint_class == "DataHandlerModule" || endpoint_class == dlhClass) {
      if (data_type == "TimeSync") {
        tsNetDesc = rule->get_descriptor();
      }
      if (data_type == "DataRequest") {
        dlhReqInputNetDesc = rule->get_descriptor();
      }
    }
    if (data_type == "HSIEvent") {
      hsiNetDesc = rule->get_descriptor();
    }
  }
  
  auto ctb_conf = get_generator();
  if (ctb_conf ==nullptr) {
    throw(BadConf(ERS_HERE, "No CTBModule configuration given"));
  }
  if (dlhInputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH data input queue descriptor given"));
  }
  if (dlhReqInputNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH request input network descriptor given"));
  }
  if (hsiNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No HSIEvent output network descriptor given"));
  }

  // Process special Network rules!
  // Looking for Fragment rules from DFAppplications in current Session
  auto sessionApps = session->get_enabled_applications();
  std::vector<conffwk::ConfigObject> fragOutObjs;
  for (auto app : sessionApps) {
    auto dfapp = app->cast<appmodel::DFApplication>();
    if (dfapp == nullptr)
      continue;
    
    auto dfNRules = dfapp->get_network_rules();
    for (auto rule : dfNRules) {
      auto descriptor = rule->get_descriptor();
      auto data_type = descriptor->get_data_type();
      if (data_type == "Fragment") {
	std::string dreqNetUid(descriptor->get_uid_base() + dfapp->UID());
	conffwk::ConfigObject frag_conn;
	config->create(dbfile, "NetworkConnection", dreqNetUid, frag_conn);
	frag_conn.set_by_val<std::string>("data_type", descriptor->get_data_type());
	frag_conn.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
	
	auto serviceObj = descriptor->get_associated_service()->config_object();
	frag_conn.set_obj("associated_service", &serviceObj);
	fragOutObjs.push_back(frag_conn);
      } // If network rule has TriggerDecision type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps
  
  // start building the list of outputs
  std::vector<const conffwk::ConfigObject*> fh_output_objs;
  for (auto& fNet : fragOutObjs) {
    fh_output_objs.push_back(&fNet);
  }

  std::vector<conffwk::ConfigObject> ctb_module_outputs;
  auto sources = get_sources();

  for ( const auto & s : sources ) {
    if (s.second == nullptr) {
      throw(BadConf(ERS_HERE, "No SourceIDConf given for " + s.first));
    }

    auto id = s.second->get_sid();
    // ----------------------------
    // create DLH
    // ----------------------------    
    auto det_id = 1; // TODO Eric Flumerfelt <eflumerf@fnal.gov>, 08-Feb-2024: This is a magic number corresponding to kDAQ
    std::string uid("DLH-" + s.first);
    conffwk::ConfigObject dlhObj;
    TLOG() << "creating OKS configuration object for " + s.first + " Data Link Handler class " << dlhClass << ", id " << id;
    config->create(dbfile, dlhClass, uid, dlhObj);
    dlhObj.set_by_val<uint32_t>("source_id", id);
    dlhObj.set_by_val<uint32_t>("detector_id", det_id);
    dlhObj.set_by_val<bool>("post_processing_enabled", false);
    dlhObj.set_obj("module_configuration", &dlhConf->config_object());
    
    auto net_objc(fh_output_objs);
    
    // Time Sync network connection
    if (dlhConf->get_generate_timesync()) {
      std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
      auto tsServiceObj = tsNetDesc->get_associated_service()->config_object();
      conffwk::ConfigObject tsNetObj;
      config->create(dbfile, "NetworkConnection", tsStreamUid, tsNetObj);
      tsNetObj.set_by_val<std::string>("connection_type", tsNetDesc->get_connection_type());
      tsNetObj.set_by_val<std::string>("data_type", tsNetDesc->get_data_type());
      tsNetObj.set_obj("associated_service", &tsServiceObj);

      net_objc.push_back(&tsNetObj);
    }

    dlhObj.set_objs("outputs", net_objc);

    // create Queues from CTB to DLH

    std::string dataQueueUid(dlhInputQDesc->get_uid_base() + s.first);
    conffwk::ConfigObject queueObj;
    config->create(dbfile, "QueueWithSourceId", dataQueueUid, queueObj);
    queueObj.set_by_val<std::string>("data_type", dlhInputQDesc->get_data_type());
    queueObj.set_by_val<std::string>("queue_type", dlhInputQDesc->get_queue_type());
    queueObj.set_by_val<uint32_t>("capacity", dlhInputQDesc->get_capacity());
    queueObj.set_by_val<uint32_t>("source_id", id);

    ctb_module_outputs.push_back(queueObj);

    // Create network connections to DLHs
    
    auto faServiceObj = dlhReqInputNetDesc->get_associated_service()->config_object();
    std::string faNetUid = dlhReqInputNetDesc->get_uid_base() + UID() + '_' + s.first;
    conffwk::ConfigObject faNetObj;
    config->create(dbfile, "NetworkConnection", faNetUid, faNetObj);
    faNetObj.set_by_val<std::string>("connection_type", dlhReqInputNetDesc->get_connection_type());
    faNetObj.set_by_val<std::string>("data_type", dlhReqInputNetDesc->get_data_type());
    faNetObj.set_obj("associated_service", &faServiceObj);

    dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

    modules.push_back(config->get<DataHandlerModule>(uid));

    
  }  // loop over CTB sources
   
  
  auto hsiServiceObj = hsiNetDesc->get_associated_service()->config_object();
  std::string hsiNetUid = hsiNetDesc->get_uid_base();
  conffwk::ConfigObject hsiNetObj;
  config->create(dbfile, "NetworkConnection", hsiNetUid, hsiNetObj);
  hsiNetObj.set_by_val<std::string>("connection_type", hsiNetDesc->get_connection_type());
  hsiNetObj.set_by_val<std::string>("data_type", hsiNetDesc->get_data_type());
  hsiNetObj.set_obj("associated_service", &hsiServiceObj);

  ctb_module_outputs.push_back(hsiNetObj);
  
  auto board = get_board();
  
  auto json = board -> get_ctb_json(*session);
  std::cout << json << std::endl;

  conffwk::ConfigObject module_obj;
  std::string module_name = "ctb-module";
  config->create(dbfile, "CTBModule", module_name, module_obj);
  module_obj.set_obj("configuration", & ctb_conf -> config_object() );
  module_obj.set_obj("board", & board -> config_object() );

  std::vector<const conffwk::ConfigObject*> ctb_module_output_ptrs;
  for ( const auto & o : ctb_module_outputs ) {
    ctb_module_output_ptrs.push_back( & o );
  }
  
  module_obj.set_objs("outputs", ctb_module_output_ptrs);
  
  auto module = config->get<appmodel::CTBModule>(module_obj);
  
  modules.push_back(module);
  
  return modules;
}



nlohmann::json CTBoardConf::get_ctb_json(const dunedaq::confmodel::Session& session) const {

  nlohmann::json json;
  json["sockets"] = get_sockets() -> to_json(false, true);
  json["misc"] = get_misc() -> get_ctb_json(session);

  nlohmann::json hlt;

  // constant block that we don't even want to configure
  auto & mask = hlt["command_mask"];
  mask["12"]="0x0";
  mask["13"]="0x0";
  mask["14"]="0x0";
  mask["15"]="0x0";

  auto hlts = get_HLTs();

  std::list<nlohmann::json> json_hlts;
  for ( const auto & hlt : hlts ) {
    json_hlts.push_back(hlt->get_ctb_json(session));
  }

  hlt["trigger"] = nlohmann::json(json_hlts);
  
  json["HLT"] = hlt;

  // --------------------------
  // Subsystems
  // --------------------------
  
  auto & subsystems = json["subsystems"];

  //  ---- Beam ----

  auto & beam_block = subsystems["beam"] = get_beam() -> to_json(false, true);
  std::list<nlohmann::json> json_beam_llts;
  auto beam_llts = get_beam_LLTs();
  for ( const auto & llt : beam_llts ) {
    json_beam_llts.push_back(llt->get_ctb_json(session));
  }
  
  beam_block["triggers"] = nlohmann::json(json_beam_llts);

  //  ---- CRT ----
  
  auto & crt_block = subsystems["crt"] = get_crt() -> to_json(false, true);
  std::list<nlohmann::json> json_crt_llts;
  auto crt_llts = get_crt_LLTs();
  for ( const auto & llt : crt_llts ) {
    json_crt_llts.push_back(llt->get_ctb_json(session));
  }
  crt_block["triggers"] = nlohmann::json(json_crt_llts);

  //  ---- PDS ----
  
  subsystems["pds"] = get_pds() -> to_json(false, true);

  nlohmann::json ret;
  ret["ctb"] = json;
  
  return ret;

}




nlohmann::json CTBMisc::get_ctb_json(const dunedaq::confmodel::Session& session) const {

  nlohmann::json ret;
  ret["randomtrigger_1"] = get_randomtrigger_1() -> get_ctb_json(session);
  ret["randomtrigger_2"] = get_randomtrigger_2() -> get_ctb_json(session);
  ret["pulser"] = get_pulser() -> to_json(false, true);
  ret["timing"] = get_timing() ->  to_json(false, true);

  static std::string ch_status_flag = "ch_status";
  if ( get_ch_status() ) ret[ch_status_flag] = true;
  else ret[ch_status_flag] = false;

  static std::string standalong_flag = "standalone_enable";
  ret[standalong_flag] = false;
  
  return ret;

}




nlohmann::json CTBTrigger::get_ctb_json(const dunedaq::confmodel::Session& session) const {

  auto json = this -> to_json(false, true);
  static std::string enable_tag = "enable";
  if ( this -> disabled(session) ) {
    json[enable_tag] = false;
  }
  else {
    json[enable_tag] = true;
  }

  json["id"] = this -> UID();
  
  return json;

}


