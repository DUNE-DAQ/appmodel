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
#include "coredal/ReadoutInterface.hpp"
#include "coredal/ReadoutGroup.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Service.hpp"
#include "coredal/Session.hpp"

#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"
#include "appdal/QueueConnectionRule.hpp"
#include "appdal/QueueDescriptor.hpp"
#include "appdal/ReadoutModule.hpp"

#include "appdal/LatencyBuffer.hpp"
#include "appdal/RequestHandler.hpp"

#include "appdal/ModuleLevelTriggerConf.hpp"
#include "appdal/ModuleLevelTrigger.hpp"

#include "appdal/StandaloneCandidateMakerConf.hpp"
#include "appdal/StandaloneCandidateMaker.hpp"

#include "appdal/TimingTriggerCandidateMakerConf.hpp"
#include "appdal/TimingTriggerCandidateMaker.hpp"

#include "appdal/CustomTriggerCandidateMakerConf.hpp"
#include "appdal/CustomTriggerCandidateMaker.hpp"

#include "appdal/TCSetTee.hpp"
#include "appdal/TCBufferConf.hpp"
#include "appdal/TCBuffer.hpp"

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
create_network_connection(std::string uid,
                          const NetworkConnectionDescriptor* ntDesc,
                          oksdbinterfaces::Configuration* confdb,
                          const std::string& dbfile)
{
  auto ntServiceObj = ntDesc->get_associated_service()->config_object();
  oksdbinterfaces::ConfigObject ntObj;
  confdb->create(dbfile, "NetworkConnection", uid, ntObj);
  ntObj.set_by_val<std::string>("data_type", ntDesc->get_data_type());
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

  // Trigger Interrupt network descriptor
  const NetworkConnectionDescriptor* tiMLTNetDesc = nullptr;
  // TriggerDecisions network descriptor
  const NetworkConnectionDescriptor* tdMLTNetDesc = nullptr;
  // Timing trigger network descriptor
  const NetworkConnectionDescriptor* tmgTrgNetDesc = nullptr;

  // Generic TC queue descriptor
  const QueueDescriptor* tcQueueDesc;

  /**************************************************************
   * Get all the queue connection descriptions
   **************************************************************/
  for (auto rule : get_queue_rules()) {
    std::string destination_class = rule->get_destination_class();
    std::string data_type = rule->get_descriptor()->get_data_type();
    if((destination_class == "ModuleLevelTrigger") && (data_type == "TriggerCandidate")){
      tcQueueDesc = rule->get_descriptor();
    }
  }
  if(!tcQueueDesc){
    throw (BadConf(ERS_HERE, "No description for the queue of TCs going tinto the MLT given"));
  }

  /**************************************************************
   * Get all the network connections
   **************************************************************/
  size_t nReadoutLinks = 0;
  for(auto rule : get_network_rules()){
    std::string endpoint_class = rule->get_endpoint_class();
    std::string data_type = rule->get_descriptor()->get_data_type();

    // Network connections for the MLT
    if(data_type == "TriggerInhibit"){
      tiMLTNetDesc = rule->get_descriptor();
    }
    if(data_type == "TriggerDecision"){
      tdMLTNetDesc = rule->get_descriptor();
    }
    if(data_type == "HSIEvent"){
      tmgTrgNetDesc = rule->get_descriptor();
    }

    TLOG_DEBUG(3) << "Endpoint class (currently not used in for networkconnections): " << endpoint_class << " data_type: " << data_type;
  }

  if(!tdMLTNetDesc){
    throw (BadConf(ERS_HERE, "No MLT network connection for the output TriggerDecision given"));
  }

  /**************************************************************
   * Get the MLT
   **************************************************************/
  auto mlt_conf = get_mlt_conf();
  // Don't build the rest if we have no MLT
  if(!mlt_conf){
    throw (BadConf(ERS_HERE, "No MLT configuration in TriggerApplication given"));
  }

  // Vector of MLT inputs and output connections
  std::vector<oksdbinterfaces::ConfigObject> mlt_inputs_copies;
  std::vector<const oksdbinterfaces::ConfigObject*> mlt_inputs;
  std::vector<const oksdbinterfaces::ConfigObject*> mlt_outputs;

  // Create MLT config object
  auto mlt_conf_obj = mlt_conf->config_object();
  oksdbinterfaces::ConfigObject mltObj;
  std::string mltUid("mlt-" + UID());
  confdb->create(dbfile, "ModuleLevelTrigger", mltUid, mltObj);
  mltObj.set_obj("configuration", &mlt_conf_obj);

  // Create TC queue to the MLT
  std::string tcQueueToMLTID(tcQueueDesc->get_uid_base() + UID());
  oksdbinterfaces::ConfigObject tcQueueToMLTObj;
  confdb->create(dbfile, "Queue", tcQueueToMLTID, tcQueueToMLTObj);
  tcQueueToMLTObj.set_by_val<std::string>("data_type", tcQueueDesc->get_data_type());
  tcQueueToMLTObj.set_by_val<std::string>("queue_type", tcQueueDesc->get_queue_type());
  tcQueueToMLTObj.set_by_val<uint32_t>("capacity", tcQueueDesc->get_capacity());
  //rndTCMakerObj.set_objs("outputs", {&tcQueueToMLTObj});
  mlt_inputs.push_back(&tcQueueToMLTObj);

  /**************************************************************
   * Create the readout map for MLT
   **************************************************************/
  auto resources = get_contains();
  if (resources.size() == 0) {
    throw (BadConf(ERS_HERE, "No ReadoutGroups contained in application"));
  }

  std::vector<const oksdbinterfaces::ConfigObject*> sourceIdConfs;
  TLOG_DEBUG(7) << "Number of ReadoutGroups in the application: " << resources.size();
  // Interate over all the readout groups
  for(auto roGroup : resources){
    if (roGroup->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled ReadoutGroup " << roGroup->UID();
      continue;
    }

    auto group_rset = roGroup->cast<coredal::ReadoutGroup>();
    if (group_rset == nullptr) {
        throw (BadConf(ERS_HERE, "TriggerApplication's readoutgroup list contains something other than ReadoutGroup"));
    }
    if (group_rset->get_contains().empty()) {
        throw (BadConf(ERS_HERE, "ReadoutGroup does not contain interfaces"));
    }

    // Iterate over each interface in per group
    auto interfaces = group_rset->get_contains();
    TLOG_DEBUG(7) << "Number of ReadoutInterfaces in that group : " << interfaces.size();
    for (auto interface_rset : interfaces) {
      if (interface_rset->disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled ReadoutInterface " << interface_rset->UID();
        continue;
      }
      auto interface = interface_rset->cast<coredal::ReadoutInterface>();
      if (interface == nullptr) {
        throw (BadConf(ERS_HERE, "ReadoutGroup contains something othen than ReadoutInterface"));
      }
      auto streams = interface->get_contains();
      TLOG_DEBUG(7) << "Number of streams in that interface: " << streams.size();

      // Interate over all the streams
      for (auto link : streams) {
        // TODO: In the future need to check if this is DROStreamConf, or some other e.g. TCBufferLink, etc
        // TODO: For now we only have DROStreamConf!
        auto stream = link->cast<coredal::DROStreamConf>();
        if (stream == nullptr) {
          throw (BadConf(ERS_HERE, "ReadoutInterface contains something other than DROStreamConf"));
        }
        if (stream->disabled(*session)) {
          TLOG_DEBUG(7) << "Ignoring disabled DROStreamConf " << stream->UID();
          continue;
        }

        // Create SourceIDConf object for the MLT
         auto id    = stream->get_source_id();
         oksdbinterfaces::ConfigObject sourceIdConf;
         std::string sourceIdConfId("dro-mlt-stream-config-");
         confdb->create(dbfile, "SourceIDConf", sourceIdConfId + std::to_string(sourceIdConfs.size()), sourceIdConf);
         sourceIdConf.set_by_val<uint32_t>("id", id);
         // https://github.com/DUNE-DAQ/daqdataformats/blob/5b99506675a586c8a09123900e224f2371d96df9/include/daqdataformats/detail/SourceID.hxx#L108
         sourceIdConf.set_by_val<std::string>("subsystem", "Detector_Readout");
         sourceIdConfs.push_back(&sourceIdConf);
      }
    }
  }
  TLOG_DEBUG(3) << "Number of mandatory readout links: " << sourceIdConfs.size();
  mltObj.set_objs("mandatory_links", sourceIdConfs);

  // Network connection for the MLT: input TriggerInhibit
  if(!tiMLTNetDesc){
      throw (BadConf(ERS_HERE, "No TriggerInhibit network connection provided for the MLT"));
  }

  oksdbinterfaces::ConfigObject tiMLTNetObj = create_network_connection(tiMLTNetDesc->get_uid_base(), tiMLTNetDesc, confdb, dbfile);
  mlt_inputs.push_back(&tiMLTNetObj);
  // Network connection for the MLT: output TriggerDecision
  oksdbinterfaces::ConfigObject tdMLTNetObj = create_network_connection(tdMLTNetDesc->get_uid_base(), tdMLTNetDesc, confdb, dbfile);
  mlt_outputs.push_back(&tdMLTNetObj);

  /**************************************************************
   * Get all the standalone trigger candidate makers
   **************************************************************/
  // TODO: Either add all the tees & buffers, or create new readout-like modules
  int nStandaloneMakers = 0;
  for(auto tcmaker_conf : get_standalone_candidate_maker_confs()){
    // Get the name of the underlying class
    std::string tcmaker_class = tcmaker_conf->get_template_for();
    if(tcmaker_class == "StandaloneCandidateMaker"){
      throw (BadConf(ERS_HERE, "\"template_for\" option not provided for one of the StandaloneCandidateMakerConf"));
    }
    TLOG_DEBUG(3) << "Adding standalone TCMaker class of type: " << tcmaker_class;

    // Make object
    auto tcmakerConfObj = tcmaker_conf->config_object();
    oksdbinterfaces::ConfigObject tcmakerObj;
    std::string uniqueUid = std::to_string(nStandaloneMakers) + UID();
    std::string tcmakerUid("standalone_maker-" + uniqueUid);
    confdb->create(dbfile, tcmaker_class.c_str(), tcmakerUid, tcmakerObj);

    // Configure the TCMaker
    tcmakerObj.set_obj("configuration", &tcmakerConfObj);
    tcmakerObj.set_objs("outputs", {&tcQueueToMLTObj});

    // Add HSI network connection if it's the TimingTrigger
    if(tcmaker_class == "TimingTriggerCandidateMaker"){
      if(!tmgTrgNetDesc){
        throw (BadConf(ERS_HERE, "No timing trigger input network connection given"));
      }
      oksdbinterfaces::ConfigObject tmgNetObj = create_network_connection(tmgTrgNetDesc->get_uid_base(), tmgTrgNetDesc, confdb, dbfile);
      tcmakerObj.set_objs("inputs", {&tmgNetObj});
    }


    //// Create the queue to the tee
    //std::string tcmakerToTeeQueueUid("tcmaker-to-tee-" + uniqueUid);
    //oksdbinterfaces::ConfigObject queueTCMakerTeeObj;
    //confdb->create(dbfile, "Queue", tcmakerToTeeQueueUid + UID(), queueTCMakerTeeObj);
    //queueTCMakerTeeObj.set_by_val<std::string>("data_type", tcQueueDesc->get_data_type());
    //queueTCMakerTeeObj.set_by_val<std::string>("queue_type", tcQueueDesc->get_queue_type());
    //queueTCMakerTeeObj.set_by_val<uint32_t>("capacity", tcQueueDesc->get_capacity());
    //tcmakerObj.set_objs("outputs", {&queueTCMakerTeeObj});

    //// Create the tee itself
    //std::string tcmTCTeeUid("tcmaker-TCSetTee-");
    //oksdbinterfaces::ConfigObject tcTeeTCMakerObj;
    //confdb->create(dbfile, "TCSetTee", tcmakerTCTeeUid + UID(), tcTeeTCMakerObj);
    //tcTeeTCMakerObj.set_objs("inputs", {&queueTCMakerTeeObj});

    //// Queue to the buffer
    //std::string tcmakerTeeToBufferQueueUid("tcmaker-tctee-to-buffer-");
    //oksdbinterfaces::ConfigObject queueTCMakerTeeToBufferObj;
    //confdb->create(dbfile, "Queue", tcmakerTeeToBufferQueueUid + UID(), queueTCMakerTeeToBufferObj);
    //queueTCMakerTeeToBufferObj.set_by_val<std::string>("data_type", tcQueueDesc->get_data_type());
    //queueTCMakerTeeToBufferObj.set_by_val<std::string>("queue_type", tcQueueDesc->get_queue_type());
    //queueTCMakerTeeToBufferObj.set_by_val<uint32_t>("capacity", tcQueueDesc->get_capacity());

    //// Queue to the MLT
    //std::string tcmakerTeeToMLTQueueUid("tcmaker-tctee-to-mlt-");
    //oksdbinterfaces::ConfigObject queueTCMakerTeeToMLTObj;
    //confdb->create(dbfile, "Queue", tcmakerTeeToMLTQueueUid + UID(), queueTCMakerTeeToMLTObj);
    //queueTCMakerTeeToMLTObj.set_by_val<std::string>("data_type", tcQueueDesc->get_data_type());
    //queueTCMakerTeeToMLTObj.set_by_val<std::string>("queue_type", tcQueueDesc->get_queue_type());
    //queueTCMakerTeeToMLTObj.set_by_val<uint32_t>("capacity", tcQueueDesc->get_capacity());

    //// The TCBuffer
    //std::string tcmakerBufferUid("tcmaker-tcbuffer");
    //oksdbinterfaces::ConfigObject tcBuffetcmakerakerObj;
    //confdb->create(dbfile, "TCBuffer", tcmakerBufferUid + UID(), tcBuffetcmakerakerObj);

    //auto tcBufferConf       = rndTCMakerConf->get_tcbuffer_conf();
    //auto tc_buffer_conf_obj = tcBufferConf->config_object();

    //tcBuffetcmakerakerObj.set_obj("configuration", &tc_buffer_conf_obj);
    //tcBuffetcmakerakerObj.set_objs("inputs", {&queueTCMakerTeeToBufferObj});
    //modules.push_back(confdb->get<TCBuffer>(tcmakerBufferUid + UID()));

    //tcTeeTCMakerObj.set_objs("outputs", {&queueTCMakerTeeToBufferObj, &queueTCMakerTeeToMLTObj});
    //mlt_inputs.push_back(std::move(&queueTCMakerTeeToMLTObj));

    //// Push all the modules related with the TCMaker 
    modules.push_back(confdb->get<StandaloneCandidateMaker>(tcmakerUid));
    //modules.push_back(confdb->get<TCSetTee>(tcmakerTCTeeUid + UID()));
    nStandaloneMakers++;
  }

  // TODO: Warning! Not finished yet, remove and move down.
  mltObj.set_objs("inputs", mlt_inputs);
  mltObj.set_objs("outputs", mlt_outputs);

  modules.push_back(confdb->get<ModuleLevelTrigger>(mltUid));

  /**************************************************************
   * Get the trigger graph
   **************************************************************/
  // Don't bother building the entire trigger graph if we have no tpstreams in.
  if(!nReadoutLinks){
    return modules;
  }

  // Get the triggering algos handler
  auto trigger_algs = get_trigger_algs();
  // Don't build the rest if we don't have triggering algs
  if(trigger_algs.size() == 0){
    return modules;
  }

  // Make the TPChannelFilters
  auto tpChannelFilterConf = get_tpchannelfilter_conf();
  for(size_t tpLinkId = 0; tpLinkId < nReadoutLinks; ++tpLinkId){
    auto tpChannelFilterConfObj = tpChannelFilterConf->config_object();
  }

  // <Trigger algorithms code goes here>

  return modules;
}
