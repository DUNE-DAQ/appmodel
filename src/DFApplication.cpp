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
#include "appmodel/DataWriter.hpp"
#include "appmodel/DataWriterConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/DataStoreConf.hpp"
#include "appmodel/FilenameParams.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/TRBConf.hpp"
#include "appmodel/TriggerRecordBuilder.hpp"
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

  // Process the queue rules looking for the TriggerRecord queue between TRB and DataWriter
  const QueueDescriptor* trQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    if (destination_class == "DataWriter") {
      trQDesc = rule->get_descriptor();
    }
  }
  if (trQDesc == nullptr) { // BadConf if no descriptor between TRB and DataWriter
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerRecords!"));
  }
  // Create queue connection config object
  conffwk::ConfigObject trQueueObj;
  std::string trQueueUid(trQDesc->get_uid_base() + UID());
  confdb->create(dbfile, "Queue", trQueueUid, trQueueObj);
  fill_queue_object_from_desc(trQDesc, trQueueObj);
  // Place trigger record queue object into vector of output objs of TRB module
  trbOutputObjs.push_back(&trQueueObj);

  // Process the network rules looking for the Fragments and TriggerDecision inputs for TRB
  const NetworkConnectionDescriptor* fragNetDesc = nullptr;
  const NetworkConnectionDescriptor* trigdecNetDesc = nullptr;
  const NetworkConnectionDescriptor* tokenNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto descriptor = rule->get_descriptor();
    auto data_type = descriptor->get_data_type();
    if (data_type == "Fragment") {
      fragNetDesc = rule->get_descriptor();
    } else if (data_type == "TriggerDecision") {
      trigdecNetDesc = rule->get_descriptor();
    } else if (data_type == "TriggerDecisionToken") {
      tokenNetDesc = rule->get_descriptor();
    }
  }
  if (fragNetDesc == nullptr) { // BadConf if no descriptor for Fragments into TRB
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input Fragments!"));
  }
  if (trigdecNetDesc == nullptr) { // BadCond if no descriptor for TriggerDecisions into TRB
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input TriggerDecisions!"));
  }
  if (tokenNetDesc == nullptr) { // BadCond if no descriptor for Tokens out of DataWriter
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for output TriggerDecisionTokens!"));
  }
  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "Could not retrieve SourceIDConf"));
  }
  // Create network connection config object
  conffwk::ConfigObject fragNetObj;
  conffwk::ConfigObject trigdecNetObj;
  conffwk::ConfigObject tokenNetObj;
  std::string fragNetUid = fragNetDesc->get_uid_base() + UID();
  std::string trigdecNetUid = trigdecNetDesc->get_uid_base() + UID();
  std::string tokenNetUid = tokenNetDesc->get_uid_base();
  confdb->create(dbfile, "NetworkConnection", fragNetUid, fragNetObj);
  confdb->create(dbfile, "NetworkConnection", trigdecNetUid, trigdecNetObj);
  confdb->create(dbfile, "NetworkConnection", tokenNetUid, tokenNetObj);
  fill_netconn_object_from_desc(fragNetDesc, fragNetObj);
  fill_netconn_object_from_desc(trigdecNetDesc, trigdecNetObj);
  fill_netconn_object_from_desc(tokenNetDesc, tokenNetObj);

  // Process special Network rules!
  // Looking for DataRequest rules from ReadoutAppplications in current Session
  auto sessionApps = session->get_all_applications();
  std::vector<conffwk::ConfigObject> dreqNetObjs;
  for (auto app : sessionApps) {
    auto roapp = app->cast<appmodel::ReadoutApplication>();
    if (roapp != nullptr) {
      TLOG() << "Readout app in session: " << roapp;
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

  // Get TRB Config Object
  auto trbConf = get_trb();
  if (trbConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriter or TRB configuration given"));
  }
  auto trbConfObj = trbConf->config_object();
  trbConfObj.set_by_val<uint32_t>("source_id", get_source_id()->get_sid());
  // Prepare TRB Module Object and assign its Config Object.
  conffwk::ConfigObject trbObj;
  std::string trbUid(UID() + "-trb");
  confdb->create(dbfile, "TriggerRecordBuilder", trbUid, trbObj);
  trbObj.set_obj("configuration", &trbConfObj);
  trbObj.set_objs("inputs", { &trigdecNetObj, &fragNetObj });
  trbObj.set_objs("outputs", trbOutputObjs);
  // Push TRB Module Object from confdb
  modules.push_back(confdb->get<TriggerRecordBuilder>(trbUid));

  // Get DataWriter Config Object (only one for now, maybe more later?)
  auto dwrConfs = get_data_writers();
  if (dwrConfs.size() == 0) {
    throw(BadConf(ERS_HERE, "No DataWriter or TRB configuration given"));
  }
  uint dw_idx = 0;
  for ( auto dwrConf :dwrConfs ) {
    auto fnParamsObj = dwrConf->get_data_store_params()->get_filename_params()->config_object();
    fnParamsObj.set_by_val<std::string>("writer_identifier", fmt::format("{}_datawriter-{}", UID(), dw_idx));
    auto dwrConfObj = dwrConf->config_object();

    // Prepare DataWriter Module Object and assign its Config Object.
    conffwk::ConfigObject dwrObj;
    std::string dwrUid(fmt::format("{}-dw-{}", UID(), dw_idx));
    confdb->create(dbfile, "DataWriter", dwrUid, dwrObj);
    dwrObj.set_obj("configuration", &dwrConfObj);
    dwrObj.set_objs("inputs", { &trQueueObj });
    dwrObj.set_objs("outputs", { &tokenNetObj });
    // Push DataWriter Module Object from confdb
    modules.push_back(confdb->get<DataWriter>(dwrUid));
    ++dw_idx;
  }

  return modules;
}
