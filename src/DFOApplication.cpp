/**
 * @file DFO.cpp
 *
 * Implementation of DFOApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "oksdbinterfaces/Configuration.hpp"
#include "oks/kernel.hpp"
#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"
#include "readoutdal/DFOApplication.hpp"
#include "readoutdal/NetworkConnectionRule.hpp"
#include "readoutdal/NetworkConnectionDescriptor.hpp"
#include "readoutdal/QueueConnectionRule.hpp"
#include "readoutdal/QueueDescriptor.hpp"
#include "readoutdalIssues.hpp"
#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::readoutdal;

static ModuleFactory::Registrator
__reg__("DFOApplication", [] (const SmartDaqApplication* smartApp,
                             oksdbinterfaces::Configuration* confdb,
                             const std::string& dbfile,
                             const coredal::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<DFOApplication>();
    return app->generate_modules(confdb, dbfile, session);
  }
  );

std::vector<const coredal::DaqModule*> 
DFOApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile,
                                     const coredal::Session* session) const
{
  std::vector<const coredal::DaqModule*> modules;

  // Code to actually generate modules should go here
  std::cout << __FUNCTION__ << " -- Unimplemented function\n";

  return modules;
}
