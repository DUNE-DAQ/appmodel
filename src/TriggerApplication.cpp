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

#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"
#include "appdal/QueueConnectionRule.hpp"
#include "appdal/QueueDescriptor.hpp"
#include "appdal/ModuleLevelTriggerConf.hpp"
#include "appdal/ModuleLevelTrigger.hpp"
#include "appdal/TPChannelFilterConf.hpp"
#include "appdal/TriggeringAlgorithms.hpp"

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

  const NetworkConnectionDescriptor* tiMLTNetDesc = nullptr;
  const NetworkConnectionDescriptor* tdMLTNetDesc = nullptr;

  // TC queue descriptor for link to the MLT
  const QueueDescriptor* tcMLTQueueDesc;

  /**************************************************************
   * Get all the queue connection descriptions
   **************************************************************/
  for (auto rule : get_queue_rules()) {
    std::string destination_class = rule->get_destination_class();
    std::string data_type = rule->get_descriptor()->get_data_type();
    if((destination_class == "ModuleLevelTrigger") && (data_type == "TriggerCandidate"))
      tcMLTQueueDesc = rule->get_descriptor();
  }

  /**************************************************************
   * Get all the network connections
   **************************************************************/
  size_t nReadoutLinks = 0;
  for(auto rule : get_network_rules()){
    std::string endpoint_class = rule->get_endpoint_class();
    if(endpoint_class == "ModuleLevelTrigger")
      tiMLTNetDesc = rule->get_descriptor();
    if(endpoint_class == "DFO")
      tdMLTNetDesc = rule->get_descriptor();
  }

  /**************************************************************
   * Get the MLT
   **************************************************************/
  auto mlt_conf = get_mlt_conf();
  // Don't build the rest if we have no MLT
  if(!mlt_conf)
    return modules;

  // Vector of MLT inputs and output connections
  std::vector<const oksdbinterfaces::ConfigObject*> mlt_inputs;
  std::vector<const oksdbinterfaces::ConfigObject*> mlt_outputs;

  // Create MLT config object
  auto mlt_conf_obj = mlt_conf->config_object();
  oksdbinterfaces::ConfigObject mltObj;
  std::string mltUid("mlt");
  confdb->create(dbfile, "ModuleLevelTrigger", mltUid, mltObj);
  mltObj.set_obj("configuration", &mlt_conf_obj);

  // Add network connections to MLT
  auto tiMLTServiceObj = tiMLTNetDesc->get_associated_service()->config_object();
  std::string tiMLTNetUid("df_busy_signal-" + UID());
  oksdbinterfaces::ConfigObject tiMLTNetObj;
  confdb->create(dbfile, "NetworkConnection", tiMLTNetUid, tiMLTNetObj);
  tiMLTNetObj.set_by_val<std::string>("connection_type", tiMLTNetDesc->get_connection_type());
  tiMLTNetObj.set_obj("associated_service", &tiMLTServiceObj);
  mlt_inputs.push_back(&tiMLTNetObj);

  // Warning! Not finished yet, remove and move down.

  mltObj.set_objs("inputs", mlt_inputs);
  mltObj.set_objs("outputs", mlt_outputs);

  modules.push_back(confdb->get<ModuleLevelTrigger>(mltUid));

  /**************************************************************
   * Get the trigger graph
   **************************************************************/
  // Don't bother building the entire trigger graph if we have no tpstreams in.
  if(!nReadoutLinks)
    return modules;

  // Get the triggering algos handler
  auto trigger_algs = get_trigger_algs();
  // Don't build the rest if we don't have triggering algs
  if(trigger_algs.size() == 0)
    return modules;

  // Make the TPChannelFilters
  auto tpChannelFilterConf = get_tpchannelfilter_conf();
  for(size_t tpLinkId = 0; tpLinkId < nReadoutLinks; ++tpLinkId){
    auto tpChannelFilterConfObj = tpChannelFilterConf->config_object();
  }

  // <Trigger algorithms code goes here>

  return modules;
}
