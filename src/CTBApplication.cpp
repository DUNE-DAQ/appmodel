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

  auto json_true = ctb_conf -> to_json(true);

  auto json_false = ctb_conf -> to_json(false);
  std::cout << json_true << std::endl
	    << json_false << std::endl;
    
  return modules;
}


