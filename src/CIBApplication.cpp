/**
 * @file CIBApplication.cpp
 *
 * Implementation of CIBApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/DetDataSender.hpp"

#include "ConfigObjectFactory.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "appmodel/CIBApplication.hpp"
#include "appmodel/CIBoardConf.hpp"
#include "appmodel/CIBConf.hpp"
#include "appmodel/CIBModule.hpp"
#include "appmodel/CIBSockets.hpp"
#include "appmodel/CIBTrigger.hpp"
#include "appmodel/CIBMisc.hpp"
#include "appmodel/CIBRandomTrigger.hpp"
#include "appmodel/CIBPulser.hpp"
#include "appmodel/CIBTiming.hpp"
#include "appmodel/CIBHLT.hpp"
#include "appmodel/CIBLLT.hpp"
#include "appmodel/CIBCountLLT.hpp"
#include "appmodel/CIBSubsystem.hpp"
#include "appmodel/CIBCRTSubsystem.hpp"
#include "appmodel/CIBPDSSubsystem.hpp"
#include "appmodel/CIBStatisticsSocket.hpp"
#include "appmodel/CIBSocket.hpp"
#include "appmodel/CIBReceiverSocket.hpp"
#include "appmodel/CIBMonitorSocket.hpp"


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

std::vector<const confmodel::Resource*>
CIBApplication::contained_resources() const {
  std::vector<const confmodel::Resource*> resources;
  resources.push_back(dynamic_cast<const confmodel::Resource*>(get_board()));
  return resources;
}


std::vector<const confmodel::DaqModule*> 
CIBApplication::generate_modules(const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);
  
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
  
  auto CIB_conf = get_generator();
  if (CIB_conf ==nullptr) {
    throw(BadConf(ERS_HERE, "No CIBModule configuration given"));
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
  auto sessionApps = session->enabled_applications();
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
	conffwk::ConfigObject frag_conn = obj_fac.create_net_obj(descriptor, dreqNetUid);
	fragOutObjs.push_back(frag_conn);
      } // If network rule has TriggerDecision type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps
  
  // start building the list of outputs
  std::vector<const conffwk::ConfigObject*> fh_output_objs;
  for (auto& fNet : fragOutObjs) {
    fh_output_objs.push_back(&fNet);
  }

  std::vector<conffwk::ConfigObject> CIB_module_outputs;
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
    TLOG() << "creating OKS configuration object for " + s.first + " Data Link Handler class " << dlhClass << ", id " << id;
    std::string uid("DLH-" + s.first);
    conffwk::ConfigObject dlhObj = obj_fac.create( dlhClass, uid );
    dlhObj.set_by_val<uint32_t>("source_id", id);
    dlhObj.set_by_val<uint32_t>("detector_id", det_id);
    dlhObj.set_by_val<bool>("post_processing_enabled", false);
    dlhObj.set_obj("module_configuration", &dlhConf->config_object());
    
    auto net_objc(fh_output_objs);
    
    // Time Sync network connection
    if (dlhConf->get_generate_timesync()) {
      std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
      conffwk::ConfigObject tsNetObj = obj_fac.create_net_obj(tsNetDesc, tsStreamUid);
      net_objc.push_back(&tsNetObj);
    }

    dlhObj.set_objs("outputs", net_objc);

    // create Queues from CIB to DLH
    std::string dataQueueUid(dlhInputQDesc->get_uid_base() + s.first);
    conffwk::ConfigObject queueObj = obj_fac.create_queue_sid_obj(dlhInputQDesc, id); 
    queueObj.rename(dataQueueUid);
    
    CIB_module_outputs.push_back(queueObj);

    // Create network connections to DLHs
    std::string faNetUid = dlhReqInputNetDesc->get_uid_base() + UID() + '_' + s.first;
    conffwk::ConfigObject faNetObj = obj_fac.create_net_obj(dlhReqInputNetDesc, faNetUid);

    dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

    modules.push_back(obj_fac.get_dal<appmodel::DataHandlerModule>(uid));
    
  }  // loop over CIB sources
   

  conffwk::ConfigObject hsiNetObj = obj_fac.create_net_obj(hsiNetDesc, "");
  CIB_module_outputs.push_back(hsiNetObj);
  
  auto board = get_board();
  
  conffwk::ConfigObject module_obj = obj_fac.create( "CIBModule", "CIB-module");
  module_obj.set_obj("configuration", & CIB_conf -> config_object() );
  module_obj.set_obj("board", & board -> config_object() );
  
  std::vector<const conffwk::ConfigObject*> CIB_module_output_ptrs;
  for ( const auto & o : CIB_module_outputs ) {
    CIB_module_output_ptrs.push_back( & o );
  }
  
  module_obj.set_objs("outputs", CIB_module_output_ptrs);
  
  auto module = obj_fac.get_dal<appmodel::CIBModule>(module_obj.UID());
  
  modules.push_back(module);
  
  return modules;
}



std::vector<const confmodel::Resource*>
CIBoardConf::contained_resources() const {
  std::vector<const confmodel::Resource*> resources;
  resources.push_back(get_misc());

  auto hlts = get_HLTs();
  resources.insert(resources.end(), hlts.begin(), hlts.end());

  auto crt_llts = get_CRT_LLTs();
  resources.insert(resources.end(), crt_llts.begin(), crt_llts.end());

  auto llts = get_beam_LLTs();
  resources.insert(resources.end(), llts.begin(), llts.end());

  return resources;
}


nlohmann::json CIBoardConf::get_CIB_json(const dunedaq::confmodel::Session& session, std::optional<std::string> socket_host) const {

  nlohmann::json json;
  json["sockets"] = get_sockets() -> get_CIB_json( socket_host ); 
  json["misc"] = get_misc()->get_CIB_json(session);

  nlohmann::json hlt;

  // constant block that we don't even want to configure
  auto & mask = hlt["command_mask"];
  mask["C"]="0x0";
  mask["D"]="0x0";
  mask["E"]="0x0";
  mask["F"]="0x0";

  auto hlts = get_HLTs();

  std::list<nlohmann::json> json_hlts;
  for ( const auto & hlt : hlts ) {
    json_hlts.push_back(hlt->get_CIB_json(session));
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
    json_beam_llts.push_back(llt->get_CIB_json(session));
  }
  
  beam_block["triggers"] = nlohmann::json(json_beam_llts);

  //  ---- CRT ----
  
  auto & crt_block = subsystems["crt"] = get_CRT() -> to_json(false, true);
  std::list<nlohmann::json> json_crt_llts;
  auto crt_llts = get_CRT_LLTs();
  for ( const auto & llt : crt_llts ) {
    json_crt_llts.push_back(llt->get_CIB_json(session));
  }
  crt_block["triggers"] = nlohmann::json(json_crt_llts);

  //  ---- PDS ----
  
  subsystems["pds"] = get_pds() -> to_json(false, true);

  nlohmann::json ret;
  ret["CIB"] = json;
  
  return ret;

}

std::vector<const confmodel::Resource*> CIBMisc::contained_resources() const {
  return std::vector<const confmodel::Resource*>{ get_randomtrigger_1(), get_randomtrigger_2() };
}



nlohmann::json CIBMisc::get_CIB_json(const dunedaq::confmodel::Session& session) const {

  nlohmann::json ret;
  ret["randomtrigger_1"] = get_randomtrigger_1()->get_CIB_json(session);
  ret["randomtrigger_2"] = get_randomtrigger_2()->get_CIB_json(session);
  ret["pulser"] = get_pulser() -> to_json(false, true);
  ret["timing"] = get_timing() ->  to_json(false, true);

  static std::string ch_status_flag = "ch_status";
  if ( get_ch_status() ) ret[ch_status_flag] = true;
  else ret[ch_status_flag] = false;

  static std::string standalong_flag = "standalone_enable";
  ret[standalong_flag] = false;
  
  return ret;

}


nlohmann::json CIBTrigger::get_CIB_json(const dunedaq::confmodel::Session& session) const {

  auto json = this -> to_json(false, true);
  static std::string enable_tag = "enable";
  if ( this -> is_disabled(session) ) {
    json[enable_tag] = false;
  }
  else {
    json[enable_tag] = true;
  }

  json["id"] = this -> UID();
  
  return json;

}

nlohmann::json CIBSockets::get_CIB_json(std::optional<std::string> socket_host) const {

  nlohmann::json json;
  json["receiver"] = get_receiver() -> get_CIB_json(socket_host);
  json["monitor"] = get_monitor() -> get_CIB_json(socket_host);
  json["statistics"] = get_statistics() -> to_json(false, true);
  return json;

}

nlohmann::json CIBSocket::get_CIB_json(std::optional<std::string> socket_host) const {

  auto json = to_json(false, true);
  if ( socket_host ) {
    json["host"] = socket_host.value();
  }
  return json;

}



