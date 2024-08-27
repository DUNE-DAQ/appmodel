/**
 * @file DFApplication.cpp
 *
 * Implementation of DFApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "appmodel/DFApplication.hpp"
#include "appmodel/DataWriterModule.hpp"
#include "appmodel/DataWriterConf.hpp"
#include "appmodel/DFOBrokerModule.hpp"
#include "appmodel/DFOBrokerConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/DataStoreConf.hpp"
#include "appmodel/FilenameParams.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/TRBConf.hpp"
#include "appmodel/TRBModule.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"
#include "conffwk/Configuration.hpp"

#include <string>
#include <vector>
#include <fmt/core.h>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("DFApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<DFApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

inline void
fill_queue_object_from_desc(const QueueDescriptor* qDesc, conffwk::ConfigObject& qObj)
{
  qObj.set_by_val<std::string>("data_type", qDesc->get_data_type());
  qObj.set_by_val<std::string>("queue_type", qDesc->get_queue_type());
  qObj.set_by_val<uint32_t>("capacity", qDesc->get_capacity());
}

inline void
fill_netconn_object_from_desc(const NetworkConnectionDescriptor* netDesc, conffwk::ConfigObject& netObj)
{
  netObj.set_by_val<std::string>("data_type", netDesc->get_data_type());
  netObj.set_by_val<std::string>("connection_type", netDesc->get_connection_type());

  auto serviceObj = netDesc->get_associated_service()->config_object();
  netObj.set_obj("associated_service", &serviceObj);
}

std::vector<const confmodel::DaqModule*>
DFApplication::generate_modules(conffwk::Configuration* confdb,
                                const std::string& dbfile,
                                const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  // Containers for module specific config objects for output/input
  // Prepare TRB output objects
  std::vector<const conffwk::ConfigObject*> trbOutputObjs;

  // -- First, we process expected Queue and Network connections and create their objects.

  // Process the queue rules looking for the TriggerRecord queue between TRB and DataWriterModule, TD queue and Token Queue
  const QueueDescriptor* trQDesc = nullptr;
  const QueueDescriptor* tokenQDesc = nullptr;
  const QueueDescriptor* tdQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    if (destination_class == "DataWriterModule") {
      trQDesc = rule->get_descriptor();
    }
    if (destination_class == "DFOBrokerModule") {
      tokenQDesc = rule->get_descriptor();
    }
    if (destination_class == "TRBModule") {
      tdQDesc = rule->get_descriptor();
    }
  }
  if (trQDesc == nullptr) { // BadConf if no descriptor between TRB and DataWriterModule
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerRecords!"));
  }
  if (tokenQDesc == nullptr) { // BadConf if no descriptor between DataWriterModule and DFOBroker
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for Dataflow Tokens!"));
  }
  if (tdQDesc == nullptr) { // BadConf if no descriptor between DFOBroker and TRB
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerDecisions!"));
  }
  // Create queue connection config object
  conffwk::ConfigObject trQueueObj;
  conffwk::ConfigObject tokenQueueObj;
  conffwk::ConfigObject tdQueueObj;
  std::string trQueueUid(trQDesc->get_uid_base() + UID());
  std::string tokenQueueUid(tokenQDesc->get_uid_base() + UID());
  std::string tdQueueUid(tdQDesc->get_uid_base() + UID());
  confdb->create(dbfile, "Queue", trQueueUid, trQueueObj);
  confdb->create(dbfile, "Queue", tokenQueueUid, tokenQueueObj);
  confdb->create(dbfile, "Queue", tdQueueUid, tdQueueObj);
  fill_queue_object_from_desc(trQDesc, trQueueObj);
  fill_queue_object_from_desc(tokenQDesc, tokenQueueObj);
  fill_queue_object_from_desc(tdQDesc, tdQueueObj);
  // Place trigger record queue object into vector of output objs of TRB module
  trbOutputObjs.push_back(&trQueueObj);

  // Process the network rules looking for the Fragments and TriggerDecision inputs for TRB
  const NetworkConnectionDescriptor* fragNetDesc = nullptr;
  const NetworkConnectionDescriptor* dfodecNetDesc = nullptr;
  const NetworkConnectionDescriptor* hbNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto descriptor = rule->get_descriptor();
    auto data_type = descriptor->get_data_type();
    if (data_type == "Fragment") {
      fragNetDesc = rule->get_descriptor();
    } else if (data_type == "DFODecision") {
      dfodecNetDesc = rule->get_descriptor();
    } else if (data_type == "DataflowHeartbeat") {
      hbNetDesc = rule->get_descriptor();
    }
  }
  if (fragNetDesc == nullptr) { // BadConf if no descriptor for Fragments into TRB
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input Fragments!"));
  }
  if (dfodecNetDesc == nullptr) { // BadCond if no descriptor for DFODecisions into DFOBroker
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input DFODecisions!"));
  }
  if (hbNetDesc == nullptr) { // BadCond if no descriptor for DataflowHeartbeats out of DFOBroker
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for output DataflowHeartbeats!"));
  }
  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "Could not retrieve SourceIDConf"));
  }
  // Create network connection config object
  conffwk::ConfigObject fragNetObj;
  conffwk::ConfigObject dfodecNetObj;
  conffwk::ConfigObject hbNetObj;
  std::string fragNetUid = fragNetDesc->get_uid_base() + UID();
  std::string dfodecNetUid = dfodecNetDesc->get_uid_base() + UID();
  std::string hbNetUid = hbNetDesc->get_uid_base();
  confdb->create(dbfile, "NetworkConnection", fragNetUid, fragNetObj);
  confdb->create(dbfile, "NetworkConnection", dfodecNetUid, dfodecNetObj);
  confdb->create(dbfile, "NetworkConnection", hbNetUid, hbNetObj);
  fill_netconn_object_from_desc(fragNetDesc, fragNetObj);
  fill_netconn_object_from_desc(dfodecNetDesc, dfodecNetObj);
  fill_netconn_object_from_desc(hbNetDesc, hbNetObj);

  // Process special Network rules!
  // Looking for DataRequest rules from ReadoutAppplications in current Session
  auto sessionApps = session->get_all_applications();
  std::vector<conffwk::ConfigObject> dreqNetObjs;
  for (auto app : sessionApps) {
    auto roapp = app->cast<appmodel::ReadoutApplication>();
    if (roapp != nullptr) {
      TLOG() << "Readout app in session: " << roapp->UID();
      if (roapp->disabled(*session)) {
        TLOG() << "Ignoring disabled Readout app: " << roapp->UID();
        continue;
      }
      auto roQRules = roapp->get_network_rules();
      for (auto rule : roQRules) {
        auto descriptor = rule->get_descriptor();
        auto data_type = descriptor->get_data_type();
        if (data_type == "DataRequest") {
          dreqNetObjs.emplace_back();
          std::string dreqNetUid(descriptor->get_uid_base() + roapp->UID());
          confdb->create(dbfile, "NetworkConnection", dreqNetUid, dreqNetObjs.back());
          fill_netconn_object_from_desc(descriptor, dreqNetObjs.back());
        } // If network rule has DataRequest type of data
      }   // Loop over ReadoutApps network rules
    }     // if app is ReadoutApplication
  }       // loop over Session specific Apps

  // Get pointers to objects here, after vector has been filled so they don't move on us
  for (auto& obj : dreqNetObjs) {
    trbOutputObjs.push_back(&obj);
  }

  // -- Second, we create the Module objects and assign their configs, with the precreated
  // -- connection config objects above.

  auto brokerConf = get_broker();
  if (brokerConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DFOBroker configuration given"));
  }
  auto brokerConfObj = brokerConf->config_object();

  conffwk::ConfigObject dfobrokerObj;
  std::string dfobUid(UID() + "-dfobroker");
  confdb->create(dbfile, "DFOBrokerModule", dfobUid, dfobrokerObj);
  dfobrokerObj.set_obj("configuration", &brokerConfObj);
  dfobrokerObj.set_objs("inputs", { &dfodecNetObj, &tokenQueueObj });
  dfobrokerObj.set_objs("outputs", { &hbNetObj, &tdQueueObj });
  modules.push_back(confdb->get<DFOBrokerModule>(dfobUid));

  // Get TRB Config Object
  auto trbConf = get_trb();
  if (trbConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriterModule or TRB configuration given"));
  }
  auto trbConfObj = trbConf->config_object();
  trbConfObj.set_by_val<uint32_t>("source_id", get_source_id()->get_sid());
  // Prepare TRB Module Object and assign its Config Object.
  conffwk::ConfigObject trbObj;
  std::string trbUid(UID() + "-trb");
  confdb->create(dbfile, "TRBModule", trbUid, trbObj);
  trbObj.set_obj("configuration", &trbConfObj);
  trbObj.set_objs("inputs", { &tdQueueObj, &fragNetObj });
  trbObj.set_objs("outputs", trbOutputObjs);
  // Push TRB Module Object from confdb
  modules.push_back(confdb->get<TRBModule>(trbUid));

  // Get DataWriterModule Config Object (only one for now, maybe more later?)
  auto dwrConfs = get_data_writers();
  if (dwrConfs.size() == 0) {
    throw(BadConf(ERS_HERE, "No DataWriterModule or TRB configuration given"));
  }
  uint dw_idx = 0;
  for ( auto dwrConf :dwrConfs ) {
    // auto fnParamsObj = dwrConf->get_data_store_params()->get_filename_params()->config_object();
    // fnParamsObj.set_by_val<std::string>("writer_identifier", fmt::format("{}_datawriter-{}", UID(), dw_idx));
    auto dwrConfObj = dwrConf->config_object();

    // Prepare DataWriterModule Module Object and assign its Config Object.
    conffwk::ConfigObject dwrObj;
    std::string dwrUid(fmt::format("{}-dw-{}", UID(), dw_idx));
    confdb->create(dbfile, "DataWriterModule", dwrUid, dwrObj);
    dwrObj.set_obj("configuration", &dwrConfObj);
    dwrObj.set_objs("inputs", { &trQueueObj });
    dwrObj.set_objs("outputs", { &tokenQueueObj });
    // Push DataWriterModule Module Object from confdb
    modules.push_back(confdb->get<DataWriterModule>(dwrUid));
    ++dw_idx;
  }

  return modules;
}
