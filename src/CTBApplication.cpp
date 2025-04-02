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

  auto board = ctb_conf->get_board();
  auto json = board -> get_ctb_json(*session);

  std::cout << json << std::endl;

  conffwk::ConfigObject module_obj;
  std::string module_name = "ctb-module";
  config -> create(dbfile, "CTBModule", module_name, module_obj);
  module_obj.set_obj("board", & board -> config_object() );

  auto module = config->get<appmodel::CTBModule>(module_obj);
  
  modules.push_back(module);
  
  return modules;
}



nlohmann::json CTBoardConf::get_ctb_json(const dunedaq::confmodel::Session& session) const {

  auto json = get_sockets() -> to_json(false, true);
  return json;

}

