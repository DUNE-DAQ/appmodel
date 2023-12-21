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
#include "appdal/RandomTriggerCandidateMakerConf.hpp"
#include "appdal/RandomTriggerCandidateMaker.hpp"
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

/**
 * \brief Helper function that gets a network connection config
 *
 * \param idname Unique ID name of the config object
 * \param ntDesc Network connection descriptor object
 * \param confdb Global database configuration
 * \param dbfile Database file location
 *
 * \ret OKS configuration object for the network connection
 */
oksdbinterfaces::ConfigObject 
create_network_connection(const std::string& idname,
                          const NetworkConnectionDescriptor* ntDesc,
                          oksdbinterfaces::Configuration* confdb,
                          const std::string& dbfile)
{
  auto ntServiceObj = ntDesc->get_associated_service()->config_object();
  std::string ntUid(idname);
  oksdbinterfaces::ConfigObject ntObj;
  confdb->create(dbfile, "NetworkConnection", ntUid, ntObj);
  ntObj.set_by_val<std::string>("connection_type", ntDesc->get_connection_type());
  ntObj.set_obj("associated_service", &ntServiceObj);

  return ntObj;
}

std::vector<const coredal::DaqModule*> 
TriggerApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile,
                                     const coredal::Session* session) const 
{
  std::vector<const coredal::DaqModule*> modules;

  const NetworkConnectionDescriptor* tiMLTNetDesc = nullptr;
  const NetworkConnectionDescriptor* tdMLTNetDesc = nullptr;

  // TC queue descriptor for link to the MLT
  //const QueueDescriptor* tcMLTQueueDesc;
  //const QueueDescriptor* rtcmQueueDesc;
  const QueueDescriptor* tcQueueDesc;

  /**************************************************************
   * Get all the queue connection descriptions
   **************************************************************/
  for (auto rule : get_queue_rules()) {
    std::string destination_class = rule->get_destination_class();
    std::string data_type = rule->get_descriptor()->get_data_type();
    if((destination_class == "ModuleLevelTrigger") && (data_type == "TriggerCandidate"))
      tcQueueDesc = rule->get_descriptor();
  }


  /**************************************************************
   * Get all the network connections
   **************************************************************/
  size_t nReadoutLinks = 0;
  for(auto rule : get_network_rules()){
    std::string endpoint_class = rule->get_endpoint_class();
    std::string data_type = rule->get_descriptor()->get_data_type();

    // Network connections for the MLT
    if(endpoint_class == "ModuleLevelTrigger" && data_type == "TriggerInhibit")
      tiMLTNetDesc = rule->get_descriptor();
    if(endpoint_class == "ModuleLevelTrigger" && data_type == "TriggerDecision")
      tdMLTNetDesc = rule->get_descriptor();

    std::cout << "Endpoint class: " << endpoint_class << " data_type: " << data_type << std::endl;
  }

  if(!tdMLTNetDesc)
    throw (BadConf(ERS_HERE, "No MLT network connection for the output TriggerDecision given"));

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

  // Network connection for the MLT: input TriggerInhibit
  oksdbinterfaces::ConfigObject tiMLTNetObj = create_network_connection(std::string("df_busy_signal-") + UID(), tiMLTNetDesc, confdb, dbfile);
  mlt_inputs.push_back(&tiMLTNetObj);
  // Network connection for the MLT: output TriggerDecision
  oksdbinterfaces::ConfigObject tdMLTNetObj = create_network_connection(std::string("td_to_dfo-") + UID(), tdMLTNetDesc, confdb, dbfile);
  mlt_outputs.push_back(&tdMLTNetObj);

  // Warning! Not finished yet, remove and move down.

  mltObj.set_objs("inputs", mlt_inputs);
  mltObj.set_objs("outputs", mlt_outputs);

  modules.push_back(confdb->get<ModuleLevelTrigger>(mltUid));

  /**************************************************************
   * Get the random trigger candidate makers
   **************************************************************/
  auto rndTCMakerConf  = get_random_candidate_maker_conf();
  if(rndTCMakerConf){
    if(!tcQueueDesc)
      throw (BadConf(ERS_HERE, "No RTCM queue description given"));

    std::cout << "Making the RTCM!" << std::endl;
    // Make RTCM object
    auto rndTCMakerConfObj = rndTCMakerConf->config_object();
    oksdbinterfaces::ConfigObject rndTCMakerObj;
    std::string rndTCMakerUid("rtcm-");
    confdb->create(dbfile, "RandomTriggerCandidateMaker", rndTCMakerUid + UID(), rndTCMakerObj);
    rndTCMakerObj.set_obj("configuration", &rndTCMakerConfObj);

    // Create the queue to the tee
    std::string rtcmToTeeQueueUid("rtcm-to-tee-");
    oksdbinterfaces::ConfigObject queueRTCMTeeObj;
    confdb->create(dbfile, "Queue", rtcmToTeeQueueUid + UID(), queueRTCMTeeObj);
    queueRTCMTeeObj.set_by_val<std::string>("data_type", tcQueueDesc->get_data_type());
    queueRTCMTeeObj.set_by_val<std::string>("queue_type", tcQueueDesc->get_queue_type());
    queueRTCMTeeObj.set_by_val<uint32_t>("capacity", tcQueueDesc->get_capacity());
    rndTCMakerObj.set_objs("outputs", {&queueRTCMTeeObj});

    // Push the RTCM algorithm
    modules.push_back(confdb->get<RandomTriggerCandidateMaker>(rndTCMakerUid + UID()));
  }


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
