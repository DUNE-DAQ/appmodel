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

inline void
fill_sourceid_object_from_app(const SmartDaqApplication* smartapp,
                              const conffwk::ConfigObject* netConn,
                              conffwk::ConfigObject& sidNetObj)
{
  sidNetObj.set_obj("netconn", netConn);
  sidNetObj.set_objs("source_ids", { &smartapp->get_source_id()->config_object() });
}

inline void
fill_sourceid_object_from_app(conffwk::Configuration* confdb,
                              const std::string& dbfile,
                              const ReadoutApplication* roapp,
                              const conffwk::ConfigObject* netConn,
                              conffwk::ConfigObject& sidNetObj,
                              std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs)
{
  sidNetObj.set_obj("netconn", netConn);

  std::vector<const conffwk::ConfigObject*> source_id_objs;
  std::vector<uint32_t> app_source_ids;

  for (auto d2d_conn_res : roapp->get_contains()) {

    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader
    // module
    auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

    if (!d2d_conn) {
      continue;
    }

    // Loop over senders
    for (auto dros : d2d_conn->get_streams()) {

      auto stream = dros->cast<confmodel::DetectorStream>();
      if (!stream)
        continue;
      app_source_ids.push_back(stream->get_source_id());
    }
  }

  for (auto& source_id : app_source_ids) {
    auto stream_sid_obj = std::make_shared<conffwk::ConfigObject>();
    std::string streamSidUid(roapp->UID() + "SourceIDConf" + std::to_string(source_id));
    confdb->create(dbfile, "SourceIDConf", streamSidUid, *stream_sid_obj);
    stream_sid_obj->set_by_val<uint32_t>("sid", source_id);
    stream_sid_obj->set_by_val<std::string>("subsystem", "Detector_Readout");
    sidObjs.push_back(stream_sid_obj);
    source_id_objs.push_back(sidObjs.back().get());
  }

  for (auto tp_sid : roapp->get_tp_source_ids()) {
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

inline void
fill_sourceid_object_from_app(conffwk::Configuration* confdb,
                              const std::string& dbfile,
                              const FakeDataApplication* fdapp,
                              const conffwk::ConfigObject* netConn,
                              conffwk::ConfigObject& sidNetObj,
                              std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs)
{
  sidNetObj.set_obj("netconn", netConn);

  std::vector<const conffwk::ConfigObject*> source_id_objs;
  std::vector<uint32_t> app_source_ids;

  for (auto fdp_res : fdapp->get_contains()) {

    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader
    // module
    auto fdpc = fdp_res->cast<appmodel::FakeDataProdConf>();

    if (!fdpc) {
      continue;
    }

    app_source_ids.push_back(fdpc->get_source_id());
  }

  for (auto& source_id : app_source_ids) {
    auto stream_sid_obj = std::make_shared<conffwk::ConfigObject>();
    std::string streamSidUid(fdapp->UID() + "SourceIDConf" + std::to_string(source_id));
    confdb->create(dbfile, "SourceIDConf", streamSidUid, *stream_sid_obj);
    stream_sid_obj->set_by_val<uint32_t>("sid", source_id);
    stream_sid_obj->set_by_val<std::string>("subsystem", "Detector_Readout");
    sidObjs.push_back(stream_sid_obj);
    source_id_objs.push_back(sidObjs.back().get());
  }

  sidNetObj.set_objs("source_ids", source_id_objs);
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
  auto sessionApps = session->get_enabled_applications();
  std::vector<conffwk::ConfigObject> dreqNetObjs;
  std::vector<conffwk::ConfigObject> sidNetObjs;
  std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs;
  for (auto app : sessionApps) {
    auto smartapp = app->cast<appmodel::SmartDaqApplication>();
    auto roapp = app->cast<appmodel::ReadoutApplication>();
    auto fdapp = app->cast<appmodel::FakeDataApplication>();
    auto dfapp = app->cast<appmodel::DFApplication>();
    if (smartapp == nullptr || dfapp != nullptr)
      continue;
    auto src_id_check = smartapp->get_source_id();
    if (roapp == nullptr && fdapp == nullptr && src_id_check == nullptr) {
      continue;
    }

    auto roQRules = smartapp->get_network_rules();
    for (auto rule : roQRules) {
      auto descriptor = rule->get_descriptor();
      auto data_type = descriptor->get_data_type();
      if (data_type == "DataRequest") {
        std::string dreqNetUid(descriptor->get_uid_base() + smartapp->UID());
        dreqNetObjs.emplace_back();
        confdb->create(dbfile, "NetworkConnection", dreqNetUid, dreqNetObjs.back());
        fill_netconn_object_from_desc(descriptor, dreqNetObjs.back());

        std::string sidToNetUid(descriptor->get_uid_base() + smartapp->UID() + "-sids");
        sidNetObjs.emplace_back();
        confdb->create(dbfile, "SourceIDToNetworkConnection", sidToNetUid, sidNetObjs.back());
        if (roapp != nullptr) {
          fill_sourceid_object_from_app(confdb, dbfile, roapp, &dreqNetObjs.back(), sidNetObjs.back(), sidObjs);
        } else if (fdapp != nullptr) {
          fill_sourceid_object_from_app(confdb, dbfile, fdapp, &dreqNetObjs.back(), sidNetObjs.back(), sidObjs);
        } else {
          fill_sourceid_object_from_app(smartapp, &dreqNetObjs.back(), sidNetObjs.back());
        }
      } // If network rule has DataRequest type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps

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
  conffwk::ConfigObject trbObj;
  std::string trbUid(UID() + "-trb");
  confdb->create(dbfile, "TRBModule", trbUid, trbObj);
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
    conffwk::ConfigObject dwrObj;
    std::string dwrUid(fmt::format("{}-dw-{}", UID(), dw_idx));
    confdb->create(dbfile, "DataWriterModule", dwrUid, dwrObj);
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
