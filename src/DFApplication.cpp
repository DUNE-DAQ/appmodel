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

#include "appmodel/ConfigurationHelper.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataStoreConf.hpp"
#include "appmodel/DataWriterConf.hpp"
#include "appmodel/DataWriterModule.hpp"
#include "appmodel/FakeDataApplication.hpp"
#include "appmodel/FakeDataProdConf.hpp"
#include "appmodel/FilenameParams.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/TRBConf.hpp"
#include "appmodel/TRBModule.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"

#include <fmt/core.h>
#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("DFApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             std::shared_ptr<appmodel::ConfigurationHelper> helper) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<DFApplication>();
                                            return app->generate_modules(confdb, dbfile, helper);
                                          });

inline void
fill_sourceid_object_from_app(conffwk::Configuration* confdb,
                              const std::string& dbfile,
                              const conffwk::ConfigObject* netConn,
                              const std::string& uid,
                              const std::vector<uint32_t>& stream_source_ids,
                              const std::vector<const SourceIDConf*>& tp_source_ids,
                              conffwk::ConfigObject& sidNetObj,
                              std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs)
{
  sidNetObj.set_obj("netconn", netConn);

  std::vector<const conffwk::ConfigObject*> source_id_objs;

  for (auto& source_id : stream_source_ids) {
    auto stream_sid_obj = std::make_shared<conffwk::ConfigObject>();
    std::string streamSidUid(uid + "SourceIDConf" + std::to_string(source_id));
    confdb->create(dbfile, "SourceIDConf", streamSidUid, *stream_sid_obj);
    stream_sid_obj->set_by_val<uint32_t>("sid", source_id);
    stream_sid_obj->set_by_val<std::string>("subsystem", "Detector_Readout");
    sidObjs.push_back(stream_sid_obj);
    source_id_objs.push_back(sidObjs.back().get());
  }

  for (auto tp_sid : tp_source_ids) {
    sidObjs.push_back(std::make_shared<conffwk::ConfigObject>(tp_sid->config_object()));
    source_id_objs.push_back(sidObjs.back().get());
  }
  /*
  auto trig_sid_obj = std::make_shared<conffwk::ConfigObject>();
  std::string trgSidUid(roapp->UID() + "TRGSourceIDConf" + std::to_string(roapp->get_tp_source_id()));
  confdb->create(dbfile, "SourceIDConf", trgSidUid, *trig_sid_obj);
  trig_sid_obj->set_by_val<uint32_t>("sid", roapp->get_tp_source_id());
  trig_sid_obj->set_by_val<std::string>("subsystem", "Trigger");
  source_id_objs.push_back(sidObjs.back().get());
  */

  sidNetObj.set_objs("source_ids", source_id_objs);
}


std::vector<const confmodel::DaqModule*>
DFApplication::generate_modules(conffwk::Configuration* confdb,
                                const std::string& dbfile,
                                std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{
  std::vector<const confmodel::DaqModule*> modules;

  const ObjectFactory obj_fac = helper->object_factory();

  // Containers for module specific config objects for output/input
  // Prepare TRB output objects
  std::vector<const conffwk::ConfigObject*> trbOutputObjs;
  std::vector<const conffwk::ConfigObject*> trbSidNetObjs;

  // -- First, we process expected Queue and Network connections and create their objects.

  // Process the queue rules looking for the TriggerRecord queue between TRB and DataWriterModule
  const QueueDescriptor* trQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    if (destination_class == "DataWriterModule") {
      trQDesc = rule->get_descriptor();
    }
  }
  if (trQDesc == nullptr) { // BadConf if no descriptor between TRB and DataWriterModule
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerRecords!"));
  }
  // Create queue connection config object
  auto trQueueObj = obj_fac.create_queue_obj(trQDesc, UID());

  // Place trigger record queue object into vector of output objs of TRB module
  trbOutputObjs.push_back(&trQueueObj);

  // Process the network rules looking for the Fragments and TriggerDecision inputs for TRB
  const NetworkConnectionDescriptor* fragNetDesc = nullptr;
  const NetworkConnectionDescriptor* trigdecNetDesc = nullptr;
  const NetworkConnectionDescriptor* tokenNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
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
  if (tokenNetDesc == nullptr) { // BadCond if no descriptor for Tokens out of DataWriterModule
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for output TriggerDecisionTokens!"));
  }
  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "Could not retrieve SourceIDConf"));
  }
  // Create network connection config object

  auto fragNetObj = obj_fac.create_net_obj(fragNetDesc, UID());
  auto trigdecNetObj =  obj_fac.create_net_obj(trigdecNetDesc, UID());
  auto tokenNetObj = obj_fac.create_net_obj(tokenNetDesc, "");

  // Process special Network rules!
  // Looking for DataRequest rules from ReadoutAppplications in current Session
  std::vector<conffwk::ConfigObject> dreqNetObjs;
  std::vector<conffwk::ConfigObject> sidNetObjs;
  std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs;
  std::set<std::string> processed_apps;
  for (auto uid: helper->get_app_uids("DFApplication")) {
    processed_apps.insert(uid);
  }
  auto stream_src_ids = helper->get_stream_source_ids();
  auto tp_src_ids = helper->get_tp_source_ids();
  for (auto [uid, descriptor]:
         helper->get_netdescriptors("DataRequest", "ReadoutApplication")) {
    dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));

    std::string sidToNetUid(descriptor->get_uid_base() + uid + "-sids");
    sidNetObjs.emplace_back();
    confdb->create(dbfile, "SourceIDToNetworkConnection", sidToNetUid,
                   sidNetObjs.back());
    fill_sourceid_object_from_app(confdb,
                                  dbfile,
                                  &dreqNetObjs.back(),
                                  uid,
                                  stream_src_ids.at(uid),
                                  tp_src_ids.at(uid),
                                  sidNetObjs.back(),
                                  sidObjs);

    processed_apps.insert(uid);
  }
  for (auto [uid, descriptor]:
         helper->get_netdescriptors("DataRequest", "FakeDataApplication")) {

    dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));

    std::string sidToNetUid(descriptor->get_uid_base() + uid + "-sids");
    sidNetObjs.emplace_back();
    confdb->create(dbfile, "SourceIDToNetworkConnection", sidToNetUid,
                   sidNetObjs.back());
    fill_sourceid_object_from_app(confdb,
                                  dbfile,
                                  &dreqNetObjs.back(),
                                  uid,
                                  stream_src_ids.at(uid),
                                  tp_src_ids.at(uid),
                                  sidNetObjs.back(),
                                  sidObjs);

    processed_apps.insert(uid);
  }

  auto app_sources = helper->get_app_source_ids();
  // Now look at all Smart apps that are not Readout, FakeData or DF
  for (auto [uid, descriptor]: helper->get_netdescriptors("DataRequest")) {
    if (processed_apps.contains(uid)) {
      continue;
    }
    if (app_sources.contains(uid)) {
      dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));

      std::string sidToNetUid(descriptor->get_uid_base() + uid + "-sids");
      sidNetObjs.emplace_back(
        obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));
      sidNetObjs.back().set_obj("netconn", &dreqNetObjs.back());
      sidNetObjs.back().set_objs("source_ids", { &app_sources[uid]->config_object() });
    }
  }


  // Get pointers to objects here, after vector has been filled so they don't move on us
  for (auto& obj : dreqNetObjs) {
    trbOutputObjs.push_back(&obj);
  }
  for (auto& obj : sidNetObjs) {
    trbSidNetObjs.push_back(&obj);
  }

  // -- Second, we create the Module objects and assign their configs, with the precreated
  // -- connection config objects above.

  // Get TRB Config Object
  auto trbConf = get_trb();
  if (trbConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriterModule or TRB configuration given"));
  }
  auto trbConfObj = trbConf->config_object();
  trbConfObj.set_by_val<uint32_t>("source_id", get_source_id()->get_sid());
  // Prepare TRB Module Object and assign its Config Object.
  std::string trbUid(UID() + "-trb");
  auto trbObj = obj_fac.create("TRBModule", trbUid);
  trbObj.set_obj("configuration", &trbConfObj);
  trbObj.set_objs("inputs", { &trigdecNetObj, &fragNetObj });
  trbObj.set_objs("outputs", trbOutputObjs);
  trbObj.set_objs("request_connections", trbSidNetObjs);
  // Push TRB Module Object from confdb
  modules.push_back(confdb->get<TRBModule>(trbUid));

  // Get DataWriterModule Config Object (only one for now, maybe more later?)
  auto dwrConfs = get_data_writers();
  if (dwrConfs.size() == 0) {
    throw(BadConf(ERS_HERE, "No DataWriterModule or TRB configuration given"));
  }
  uint dw_idx = 0;
  for (auto dwrConf : dwrConfs) {
    // auto fnParamsObj = dwrConf->get_data_store_params()->get_filename_params()->config_object();
    // fnParamsObj.set_by_val<std::string>("writer_identifier", fmt::format("{}_datawriter-{}", UID(), dw_idx));
    auto dwrConfObj = dwrConf->config_object();

    // Prepare DataWriterModule Module Object and assign its Config Object.
    std::string dwrUid(fmt::format("{}-dw-{}", UID(), dw_idx));
    auto dwrObj = obj_fac.create("DataWriterModule", dwrUid);
    dwrObj.set_by_val("writer_identifier", fmt::format("{}_dw_{}", UID(), dw_idx));
    dwrObj.set_obj("configuration", &dwrConfObj);
    dwrObj.set_objs("inputs", { &trQueueObj });
    dwrObj.set_objs("outputs", { &tokenNetObj });
    // Push DataWriterModule Module Object from confdb
    modules.push_back(confdb->get<DataWriterModule>(dwrUid));
    ++dw_idx;
  }

  return modules;
}
