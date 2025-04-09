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

  auto ctb_conf = get_configuration();
  auto board = get_board();
  
  auto json = board -> get_ctb_json(*session);
  std::cout << json << std::endl;

  conffwk::ConfigObject module_obj;
  std::string module_name = "ctb-module";
  config -> create(dbfile, "CTBModule", module_name, module_obj);
  module_obj.set_obj("configuration", & ctb_conf -> config_object() );
  module_obj.set_obj("board", & board -> config_object() );
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
  nlohmann::json ret;
  ret["ctb"] = json;
  std::list<nlohmann::json> json_crt_llts;
  auto crt_llts = get_crt_LLTs();
  for ( const auto & llt : crt_llts ) {
    json_crt_llts.push_back(llt->get_ctb_json(session));
  }
  crt_block["triggers"] = nlohmann::json(json_crt_llts);

  //  ---- PDS ----
  
  subsystems["pds"] = get_pds() -> to_json(false, true);
    
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
  if ( get_standalone_enable() ) ret[standalong_flag] = true;
  else ret[standalong_flag] = false;

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


