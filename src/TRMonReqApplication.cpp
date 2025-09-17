/**
 * @file TRMonReqApplication.cpp
 *
 * Implementation of TRMonReqApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/TRMonReqApplication.hpp"
#include "ConfigObjectFactory.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataStoreConf.hpp"
#include "appmodel/DataWriterConf.hpp"
#include "appmodel/DataWriterModule.hpp"
#include "appmodel/FilenameParams.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/TRMonRequestorConf.hpp"
#include "appmodel/TRMonRequestorModule.hpp"
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

namespace dunedaq {
namespace appmodel {

void
TRMonReqApplication::generate_modules(const confmodel::Session* session) const
{

  ConfigObjectFactory obj_fac(this);

  std::vector<const conffwk::ConfigObject*> module_objects;

  // Containers for module specific config objects for output/input
  std::vector<const conffwk::ConfigObject*> trmrOutputObjs;

  // -- First, we process expected Queue and Network connections and create their objects.

  // Process the queue rules looking for the TriggerDecisionToken queue between DataWriterModule and TRMonRequestor
  const QueueDescriptor* tdtQDesc = nullptr;
  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    if (destination_class == "TRMonRequestorModule") {
      tdtQDesc = rule->get_descriptor();
    }
  }
  if (tdtQDesc == nullptr) { // BadConf if no descriptor between DataWriterModule and TRMonRequestor
    throw(BadConf(ERS_HERE, "Could not find queue descriptor rule for TriggerDecisionTokens!"));
  }
  // Create queue connection config object
  auto tdtQueueObj = obj_fac.create_queue_obj(tdtQDesc, UID());

  // Process the network rules looking for the TriggerRecord input for the DataWriter
  const NetworkConnectionDescriptor* dwNetDesc = nullptr;
  for (auto rule : get_network_rules()) {
    auto descriptor = rule->get_descriptor();
    auto data_type = descriptor->get_data_type();
    if (data_type == "TriggerRecord") {
      dwNetDesc = rule->get_descriptor();
    }
  }
  if (dwNetDesc == nullptr) { // BadConf if no descriptor for TriggerRecords into DW
    throw(BadConf(ERS_HERE, "Could not find network descriptor rule for input TriggerRecords!"));
  }
  // Create network connection config object
  auto dwInputObj = obj_fac.create_net_obj(dwNetDesc, "");

  // Process special Network rules!
  // Looking for DataRequest rules from ReadoutAppplications in current Session
  auto sessionApps = session->enabled_applications();
  std::vector<conffwk::ConfigObject> trmonreqNetObjs;
  for (auto app : sessionApps) {
    auto smartapp = app->cast<appmodel::SmartDaqApplication>();
    auto dfapp = app->cast<appmodel::DFApplication>();
    if (smartapp == nullptr || dfapp == nullptr) {
      continue;
    }

    auto dfNRules = smartapp->get_network_rules();
    for (auto rule : dfNRules) {
      auto descriptor = rule->get_descriptor();
      auto data_type = descriptor->get_data_type();
      if (data_type == "TRMonRequest") {
        trmonreqNetObjs.emplace_back(obj_fac.create_net_obj(descriptor, smartapp->UID()));
      } // If network rule has TRMonRequest type of data
    } // Loop over Apps network rules
  } // loop over Session specific Apps

  // Get pointers to objects here, after vector has been filled so they don't move on us
  for (auto& obj : trmonreqNetObjs) {
    trmrOutputObjs.push_back(&obj);
  }

  // -- Second, we create the Module objects and assign their configs, with the precreated
  // -- connection config objects above.

  // Get TRB Config Object
  auto trmrConf = get_trmonreq();
  if (trmrConf == nullptr) {
    throw(BadConf(ERS_HERE, "No TRMonRequestor configuration given"));
  }
  auto trmrConfObj = trmrConf->config_object();
  // Prepare TRMR Module Object and assign its Config Object.
  std::string trmrUid(UID() + "-trmr");
  conffwk::ConfigObject trmrObj = obj_fac.create("TRMonRequestorModule", trmrUid);
  trmrObj.set_obj("configuration", &trmrConfObj);
  trmrObj.set_objs("inputs", { &tdtQueueObj });
  trmrObj.set_objs("outputs", trmrOutputObjs);
  trmrObj.set_obj("trigger_record_destination", &dwInputObj);
  // Push TRMR Module Object from confdb
  auto dal_obj = obj_fac.get_dal<confmodel::DaqModule>(trmrUid);
  module_objects.push_back(&dal_obj->config_object());

  // Get DataWriterModule Config Object (only one for now, maybe more later?)
  auto dwrConf = get_data_writer();
  if (dwrConf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriterModule configuration given"));
  }

  auto dwrConfObj = dwrConf->config_object();

  // Prepare DataWriterModule Module Object and assign its Config Object.
  std::string dwrUid(fmt::format("{}-dw", UID()));
  conffwk::ConfigObject dwrObj = obj_fac.create("DataWriterModule", dwrUid);
  dwrObj.set_by_val("writer_identifier", fmt::format("{}_dw", UID()));
  dwrObj.set_obj("configuration", &dwrConfObj);
  dwrObj.set_objs("inputs", { &dwInputObj });
  dwrObj.set_objs("outputs", { &tdtQueueObj });
  // Push DataWriterModule Module Object from confdb
  dal_obj = obj_fac.get_dal<confmodel::DaqModule>(dwrUid);
  module_objects.push_back(&dal_obj->config_object());

  auto app_obj = obj_fac.get_dal<TRMonReqApplication>(UID())->config_object();
  app_obj.set_objs("modules", module_objects);
  configuration().update<TRMonReqApplication>({UID()}, {}, {});
}

} // namespace appmodel
} // namespace dunedaq
