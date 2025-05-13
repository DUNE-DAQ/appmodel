/**
 * @file DFO.cpp
 *
 * Implementation of Dune Timing System HSIApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"
#include "appmodel/DTSHSIApplication.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/HSIReadout.hpp"
#include "appmodel/HSIReadoutConf.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "conffwk/Configuration.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

std::vector<const confmodel::DaqModule*>
DTSHSIApplication::generate_modules(conffwk::Configuration* confdb,
                                     const std::string& dbfile,
                                     const confmodel::Session* /*session*/) const
{
  ConfigObjectFactory obj_fac(this);
  
  std::vector<const confmodel::DaqModule*> modules;

  auto dlhConf = get_link_handler();
  auto dlhClass = dlhConf->get_template_for();

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
    throw(BadConf(ERS_HERE, "No HSIEventGeneratorModule configuration given"));
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

  // Time Sync network connection
  if (dlhConf->get_generate_timesync()) {
    auto tsServiceObj = tsNetDesc->get_associated_service()->config_object();
    auto tsNetObj = obj_fac.create_net_obj(tsNetDesc, std::to_string(id));

    dlhObj.set_objs("outputs", { &tsNetObj });
  } else {
    dlhObj.set_objs("outputs", {});
  }
  conffwk::ConfigObject queueObj = obj_fac.create_queue_sid_obj(dlhInputQDesc,id);
  conffwk::ConfigObject faNetObj = obj_fac.create_net_obj(dlhReqInputNetDesc, UID());
  dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

  modules.push_back(confdb->get<DataHandlerModule>(uid));

  auto hsiServiceObj = hsiNetDesc->get_associated_service()->config_object();
  conffwk::ConfigObject hsiNetObj = obj_fac.create_net_obj(hsiNetDesc, "");
  
  std::string genuid("HSI-" + std::to_string(id));
  conffwk::ConfigObject hsiObj = obj_fac.create("HSIReadout", genuid);
  hsiObj.set_obj("configuration", &rdrConf->config_object());
  hsiObj.set_objs("outputs", { &queueObj, &hsiNetObj });

  modules.push_back(confdb->get<HSIReadout>(genuid));

  return modules;
}
 
} // namespace appmodel  
} // namespace dunedaq