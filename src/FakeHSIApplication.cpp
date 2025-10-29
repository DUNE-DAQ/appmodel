/**
 * @file FakeHSIApplication.cpp
 *
 * Implementation of FakeHSIApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"
#include "appmodel/FakeHSIApplication.hpp"
#include "appmodel/FakeHSIEventGeneratorModule.hpp"
#include "appmodel/FakeHSIEventGeneratorConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"
#include "conffwk/Configuration.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

namespace dunedaq {
namespace appmodel {

void
FakeHSIApplication::generate_modules(const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);


  auto dlhConf = get_link_handler();
  auto dlhClass = dlhConf->get_template_for();

  // 23-Sep-2025, KAB et al: prevent a mis-configuration of the system in which the
  // FakeHSI DLH is told to generate TimeSync messages. (TimeSync messages should only
  // be sent from Readout DLH modules so that we don't get confusing system behavior.)
  if (dlhConf->get_generate_timesync()) {
    throw(BadConf(ERS_HERE, "TimeSync generation is enabled for a FakeHSIApplication and this is not allowed"));
  }

  const QueueDescriptor* dlhInputQDesc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "DataHandlerModule" || destination_class == dlhClass) {
      dlhInputQDesc = rule->get_descriptor();
    }
  }

  const NetworkConnectionDescriptor* dlhReqInputNetDesc = nullptr;
  const NetworkConnectionDescriptor* tsNetDesc = nullptr;
  const NetworkConnectionDescriptor* hsiNetDesc = nullptr;

  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();

    if (endpoint_class == "DataHandlerModule" || endpoint_class == dlhClass) {
      if (data_type == "TimeSync") {
        tsNetDesc = rule->get_descriptor();
      }
      if (data_type == "DataRequest") {
        dlhReqInputNetDesc = rule->get_descriptor();
      }
    }
    if (data_type == "HSIEvent") {
      hsiNetDesc = rule->get_descriptor();
    }
  }

  auto rdrConf = get_generator();
  if (rdrConf == 0) {
    throw(BadConf(ERS_HERE, "No FakeHSIEventGeneratorModule configuration given"));
  }
  if (dlhInputQDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH data input queue descriptor given"));
  }
  if (dlhReqInputNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No DLH request input network descriptor given"));
  }
  if (hsiNetDesc == nullptr) {
    throw(BadConf(ERS_HERE, "No HSIEvent output network descriptor given"));
  }

  auto idconf = get_source_id();
  if (idconf == nullptr) {
    throw(BadConf(ERS_HERE, "No SourceIDConf given"));
  }
  auto id = idconf->get_sid();

  auto det_id = 1; // TODO Eric Flumerfelt <eflumerf@fnal.gov>, 08-Feb-2024: This is a magic number corresponding to kDAQ
  std::string uid("DLH-" + std::to_string(id));
  TLOG_DEBUG(7) << "creating OKS configuration object for Data Link Handler class " << dlhClass << ", id " << id;
  conffwk::ConfigObject dlhObj = obj_fac.create(dlhClass, uid);
  dlhObj.set_by_val<uint32_t>("source_id", id);
  dlhObj.set_by_val<uint32_t>("detector_id", det_id);
  dlhObj.set_by_val<bool>("post_processing_enabled", false);
  dlhObj.set_obj("module_configuration", &dlhConf->config_object());

  // Process special Network rules!
  // Looking for Fragment rules from DFAppplications in current Session
  auto sessionApps = session->enabled_applications();
  std::vector<conffwk::ConfigObject> fragOutObjs;
  for (auto app : sessionApps) {
    auto dfapp = app->cast<appmodel::DFApplication>();
    if (dfapp == nullptr)
      continue;

    auto dfNRules = dfapp->get_network_rules();
    for (auto rule : dfNRules) {
      auto descriptor = rule->get_descriptor();
      auto data_type = descriptor->get_data_type();
      if (data_type == "Fragment") {
        conffwk::ConfigObject frag_conn =
          obj_fac.create_net_obj(descriptor, dfapp->UID());
        fragOutObjs.push_back(frag_conn);
      } // If network rule has TriggerDecision type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps

  // start building the list of outputs
  std::vector<const conffwk::ConfigObject*> fh_output_objs;
  for (auto& fNet : fragOutObjs) {
    fh_output_objs.push_back(&fNet);
  }

  // Time Sync network connection
  if (dlhConf->get_generate_timesync()) {
    conffwk::ConfigObject tsNetObj = obj_fac.create_net_obj(tsNetDesc, std::to_string(id));
    fh_output_objs.push_back(&tsNetObj);
  }
  dlhObj.set_objs("outputs", fh_output_objs);

  conffwk::ConfigObject queueObj = obj_fac.create_queue_sid_obj(dlhInputQDesc, id);
  conffwk::ConfigObject faNetObj = obj_fac.create_net_obj(dlhReqInputNetDesc, UID());

  dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

  modules.push_back(obj_fac.get_dal<DataHandlerModule>(uid));

  auto hsiServiceObj = hsiNetDesc->get_associated_service()->config_object();
  conffwk::ConfigObject hsiNetObj = obj_fac.create_net_obj(hsiNetDesc, "");
  
  std::string genuid("FakeHSI-" + std::to_string(id));
  conffwk::ConfigObject fakehsiObj =
    obj_fac.create("FakeHSIEventGeneratorModule", genuid);
  fakehsiObj.set_obj("configuration", &rdrConf->config_object());
  fakehsiObj.set_objs("outputs", { &queueObj, &hsiNetObj });
  if (tsNetDesc != nullptr) {
    conffwk::ConfigObject tsNetObjIn = obj_fac.create_net_obj(tsNetDesc, ".*");
    fakehsiObj.set_objs("inputs", { &tsNetObjIn });
  }

  modules.push_back(obj_fac.get_dal<FakeHSIEventGeneratorModule>(genuid));

  obj_fac.update_modules(modules);
}
 
} // namespace appmodel  
} // namespace dunedaq
