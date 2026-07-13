/**
 * @file DFApplication.cpp
 *
 * Implementation of DFApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ConfigObjectFactory.hpp"
#include "appmodel/ConfigurationHelper.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataStoreConf.hpp"
#include "appmodel/DataWriterConf.hpp"
#include "appmodel/DataWriterModule.hpp"
#include "appmodel/FilenameParams.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/TRBConf.hpp"
#include "appmodel/TRBModule.hpp"
#include "appmodel/appmodelIssues.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"

#include "logging/Logging.hpp"
#include "oks/kernel.hpp"

#include <fmt/core.h>
#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {


static inline void
fill_sourceid_object(const ConfigObjectFactory& obj_fac,
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
    std::string streamSidUid(uid + "SourceIDConf" + std::to_string(source_id));
    auto stream_sid_obj = std::make_shared<conffwk::ConfigObject>(obj_fac.create("SourceIDConf", streamSidUid));
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
  std::string trgSidUid(roapp->UID() + "TRGSourceIDConf" + std::to_string(roapp->get_tp_source_id()));
  auto trig_sid_obj = std::make_shared<conffwk::ConfigObject>(obj_fac.create("SourceIDConf", trgSidUid));
  trig_sid_obj->set_by_val<uint32_t>("sid", roapp->get_tp_source_id());
  trig_sid_obj->set_by_val<std::string>("subsystem", "Trigger");
  source_id_objs.push_back(sidObjs.back().get());
  */

  sidNetObj.set_objs("source_ids", source_id_objs);
}


inline void
fill_replay_sourceid_object(const ConfigObjectFactory& obj_fac,
                            const std::string& uid,
                            const std::vector<const SourceIDConf*>& tp_source_ids,
                            std::vector<conffwk::ConfigObject>* netConn,
                            std::vector<conffwk::ConfigObject>* sidNetObj,
                            const NetworkConnectionDescriptor* descriptor,
                            std::vector<std::shared_ptr<conffwk::ConfigObject>> sidObjs)
{
  std::vector<const conffwk::ConfigObject*> source_id_objs;

  for (auto tp_sid : tp_source_ids) {
    // get name extension
    std::string name = tp_sid->UID();
    size_t pos = name.find_last_of('-');
    std::string ext;
    if (pos != std::string::npos) {
      ext = name.substr(pos);
    }

    // set Network connections
    std::string dreqNetUid(uid + ext);
    netConn->emplace_back(
      obj_fac.create_net_obj(descriptor, dreqNetUid));
    netConn->back().set_by_val<std::string>("data_type", descriptor->get_data_type());
    netConn->back().set_by_val<std::string>("connection_type", descriptor->get_connection_type());
    auto serviceObj = descriptor->get_associated_service()->config_object();
    netConn->back().set_obj("associated_service", &serviceObj);

    // set SourceID to Network connections
    std::string sidToNetUid(uid + ext + "-sids");
    sidNetObj->emplace_back(
      obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));
    sidNetObj->back().set_obj("netconn", &netConn->back());

    // set SourceID objs
    sidObjs.push_back(std::make_shared<conffwk::ConfigObject>(tp_sid->config_object()));
    sidNetObj->back().set_objs("source_ids", { sidObjs.back().get() });
  }
}



void
DFApplication::generate_modules(
  std::shared_ptr<appmodel::ConfigurationHelper> helper) const {

  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  // Containers for module specific config objects for output/input
  // Prepare TRB output objects
  std::vector<const conffwk::ConfigObject*> trbInputObjs;
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
  const NetworkConnectionDescriptor* trmonReqNetDesc = nullptr;
  const NetworkConnectionDescriptor* trmonTRNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto descriptor = rule->get_descriptor();
    auto data_type = descriptor->get_data_type();
    if (data_type == "Fragment") {
      fragNetDesc = rule->get_descriptor();
    } else if (data_type == "TriggerDecision") {
      trigdecNetDesc = rule->get_descriptor();
    } else if (data_type == "TriggerDecisionToken") {
      tokenNetDesc = rule->get_descriptor();
    } else if (data_type == "TRMonRequest") {
      trmonReqNetDesc = rule->get_descriptor();
    } else if (data_type == "TriggerRecord") {
      trmonTRNetDesc = rule->get_descriptor();
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
  conffwk::ConfigObject trmonReqNetObj;
  conffwk::ConfigObject trmonTRNetObj;
  if (trmonReqNetDesc != nullptr) {
    trmonReqNetObj = obj_fac.create_net_obj(trmonReqNetDesc, UID());
  }
  if (trmonTRNetDesc != nullptr) {
    trmonTRNetObj = obj_fac.create_net_obj(trmonTRNetDesc, "");
  }

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
    sidNetObjs.emplace_back(obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));

    fill_sourceid_object(obj_fac,
                         &dreqNetObjs.back(),
                         uid,
                         stream_src_ids.at(uid),
                         tp_src_ids.at(uid),
                         sidNetObjs.back(),
                         sidObjs);
    processed_apps.insert(uid);
  }

  for (auto [uid, descriptor]:
         helper->get_netdescriptors("DataRequest", "TPReplayApplication")) {
    fill_replay_sourceid_object(obj_fac,
                                uid,
                                tp_src_ids.at(uid),
                                &dreqNetObjs,
                                &sidNetObjs,
                                descriptor,
                                sidObjs);
    processed_apps.insert(uid);
  }




  for (auto [uid, descriptor]:
         helper->get_netdescriptors("DataRequest", "FakeDataApplication")) {
	  dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));

    std::string sidToNetUid(descriptor->get_uid_base() + uid + "-sids");
    sidNetObjs.emplace_back(obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));

    fill_sourceid_object(obj_fac,
                         &dreqNetObjs.back(),
                         uid,
                         stream_src_ids.at(uid),
                         {}, // No tp src_ids for FakeDataApplication
                         sidNetObjs.back(),
                         sidObjs);
    processed_apps.insert(uid);
  }

  // now we treat the CTB which has 2 connections related to source IDs
  const auto ctb_type = "CTBApplication";
  for (auto [uid, descriptor]: helper->get_netdescriptors("DataRequest", ctb_type)) {

    if (processed_apps.contains(uid)) {
      continue;
    }

    for ( const auto & [uid, rel_sources] :
	  helper->get_all_app_source_ids(ctb_type) ) {
      for ( auto [rel, id] : rel_sources ) {
	std::string local_uid = uid;
	local_uid += rel.find("LLT")!=std::string::npos ? "_LLT" : "_HLT";

	dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, local_uid));
	sidObjs.push_back(std::make_shared<conffwk::ConfigObject>(id->config_object()));

	std::string sidToNetUid(descriptor->get_uid_base() + local_uid);
	sidNetObjs.emplace_back(obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));
	sidNetObjs.back().set_objs("source_ids", {sidObjs.back().get()});
	sidNetObjs.back().set_obj("netconn", &dreqNetObjs.back());

      } // loop on relational sources

      processed_apps.insert(uid);
    } // loop over CTB apps
  } // loop over descriptors for the CTB apps

  auto app_sources = helper->get_app_source_ids();
  // Now look at all Smart apps that are not Readout, FakeData or DF
  for (auto [uid, descriptor]: helper->get_netdescriptors("DataRequest")) {


    if (processed_apps.contains(uid)) {
      continue;
    }
    if (app_sources.contains(uid)) {
      dreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));

      sidObjs.push_back(std::make_shared<conffwk::ConfigObject>(
                          app_sources.at(uid)->config_object()));

      std::string sidToNetUid(descriptor->get_uid_base() + uid + "-sids");
      sidNetObjs.emplace_back(obj_fac.create("SourceIDToNetworkConnection", sidToNetUid));
      sidNetObjs.back().set_objs("source_ids", {sidObjs.back().get()});
      sidNetObjs.back().set_obj("netconn", &dreqNetObjs.back());

      processed_apps.insert(uid);
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
  trbInputObjs = { &trigdecNetObj, &fragNetObj };
  if (trmonReqNetDesc != nullptr) {
    trbInputObjs.push_back(&trmonReqNetObj);
  }
  if (trmonTRNetDesc != nullptr) {
    trbOutputObjs.push_back(&trmonTRNetObj);
  }
  // Prepare TRB Module Object and assign its Config Object.
  std::string trbUid(UID() + "-trb");
  conffwk::ConfigObject trbObj = obj_fac.create("TRBModule", trbUid);
  trbObj.set_obj("configuration", &trbConfObj);
  trbObj.set_objs("inputs", trbInputObjs);
  trbObj.set_objs("outputs", trbOutputObjs);
  trbObj.set_objs("request_connections", trbSidNetObjs);
  // Push TRB Module Object from confdb
  modules.push_back(obj_fac.get_dal<TRBModule>(trbUid));

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
    conffwk::ConfigObject dwrObj = obj_fac.create("DataWriterModule", dwrUid);
    dwrObj.set_by_val("writer_identifier", fmt::format("{}_dw_{}", UID(), dw_idx));
    dwrObj.set_obj("configuration", &dwrConfObj);
    dwrObj.set_objs("inputs", { &trQueueObj });
    dwrObj.set_objs("outputs", { &tokenNetObj });
    // Push DataWriterModule Module Object from confdb
    modules.push_back(obj_fac.get_dal<DataWriterModule>(dwrUid));
    ++dw_idx;
  }

  obj_fac.update_modules(modules);
}

} // namespace appmodel
} // namespace dunedaq
