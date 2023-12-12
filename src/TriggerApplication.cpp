/**
 * @file generate_modules.cpp
 *
 * Implementation of TriggerApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "oksdbinterfaces/Configuration.hpp"
#include "oks/kernel.hpp"

#include "coredal/Connection.hpp"
#include "coredal/DROStreamConf.hpp"
#include "coredal/NetworkConnection.hpp"
#include "coredal/ReadoutGroup.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Service.hpp"
#include "coredal/Session.hpp"

//#include "appdal/DataReader.hpp"
//#include "appdal/DataReaderConf.hpp"
#include "appdal/ReadoutModule.hpp"
//#include "appdal/DLH.hpp"
//#include "appdal/TPHandler.hpp"
//#include "appdal/FragmentAggregator.hpp"
//#include "appdal/ReadoutModelConf.hpp"
#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"
#include "appdal/QueueConnectionRule.hpp"
#include "appdal/QueueDescriptor.hpp"
//#include "appdal/TPHandlerConf.hpp"

#include "appdal/TriggerApplication.hpp"

#include "appdal/appdalIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appdal;

static ModuleFactory::Registrator
__reg__("TriggerApplication", [] (const SmartDaqApplication* smartApp,
                                  oksdbinterfaces::Configuration* confdb,
                                  const std::string& dbfile,
                                  const coredal::Session* session) -> ModuleFactory::ReturnType
  {
    auto app = smartApp->cast<TriggerApplication>();
    return app->generate_modules(confdb, dbfile, session);
  });

std::vector<const coredal::DaqModule*> 
TriggerApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile,
                                     const coredal::Session* session) const 
{
  std::vector<const coredal::DaqModule*> modules;

  // TPSet stream input from the readout
  std::vector<const NetworkConnectionDescriptor*> tpNetDescs;

  // Get all the network connections
  size_t nReadoutLinks = 0;
  for(auto rule : get_network_rules()){
    std::string endpoint_class = rule->get_endpoint_class();

    // Connection for tpstream input
    if(endpoint_class == "TPHandler"){
      tpNetDescs.push_back(rule->get_descriptor());
      std::cout << "Eureka, found a TP stream connection!" << std::endl;
    }
    std::cout << "Connection endpoint class: " << endpoint_class << std::endl;
  }
  nReadoutLinks = tpNetDescs.size();

  std::cout << "In total, we have " << nReadoutLinks << " TPSet inputs from the readout" << std::endl;


  // Don't bother building the entire trigger graph if we have no tpstreams in.
  //if(nReadoutLinks == 0)
  //  return modules;

  //for(int idReadoutLink = 0; idReadoutLink < nReadoutLinks; ++idReadoutLink){
  //  auto tpServiceObj = tpBetDescs[i]->get_associated_service()->config_object();

  //  confdb->create(dbfile, "NetworkConnection", ____ID____, tpServiceObj);

  //}

  return modules;
}
