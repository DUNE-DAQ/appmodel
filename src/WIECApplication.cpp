/**
 * @file DFO.cpp
 *
 * Implementation of WIECApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "appmodel/WIECApplication.hpp"


#include <string>
#include <vector>
#include <iostream>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator
__reg__("WIECApplication", [] (const SmartDaqApplication* smartApp,
                             conffwk::Configuration* confdb,
                             const std::string& dbfile,
                             const confmodel::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<WIECApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const confmodel::DaqModule*> 
WIECApplication::generate_modules(conffwk::Configuration* confdb,
                                            const std::string& dbfile,
                                            const confmodel::Session* /*session*/) const
{
  std::vector<const confmodel::DaqModule*> modules;

  return modules;
}
