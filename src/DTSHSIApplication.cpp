/**
 * @file DFO.cpp
 *
 * Implementation of Dune Timing System HSIApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

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
#include "oks/kernel.hpp"
#include "conffwk/Configuration.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("DTSHSIApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<DTSHSIApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

std::vector<const confmodel::DaqModule*>
DTSHSIApplication::generate_modules(conffwk::Configuration* confdb,
                                     const std::string& dbfile,
                                     const confmodel::Session* /*session*/) const
{
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
  conffwk::ConfigObject dlhObj;
  TLOG_DEBUG(7) << "creating OKS configuration object for Data Link Handler class " << dlhClass << ", id " << id;
  confdb->create(dbfile, dlhClass, uid, dlhObj);
  dlhObj.set_by_val<uint32_t>("source_id", id);
  dlhObj.set_by_val<uint32_t>("detector_id", det_id);
  dlhObj.set_by_val<bool>("post_processing_enabled", false);
  dlhObj.set_obj("module_configuration", &dlhConf->config_object());

  // Time Sync network connection
  if (dlhConf->get_generate_timesync()) {
    std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
    auto tsServiceObj = tsNetDesc->get_associated_service()->config_object();
    conffwk::ConfigObject tsNetObj;
    confdb->create(dbfile, "NetworkConnection", tsStreamUid, tsNetObj);
    tsNetObj.set_by_val<std::string>("connection_type", tsNetDesc->get_connection_type());
    tsNetObj.set_by_val<std::string>("data_type", tsNetDesc->get_data_type());
    tsNetObj.set_obj("associated_service", &tsServiceObj);

    dlhObj.set_objs("outputs", { &tsNetObj });
  } else {
    dlhObj.set_objs("outputs", {});
  }
  std::string dataQueueUid(dlhInputQDesc->get_uid_base() + std::to_string(id));
  conffwk::ConfigObject queueObj;
  confdb->create(dbfile, "QueueWithSourceId", dataQueueUid, queueObj);
  queueObj.set_by_val<std::string>("data_type", dlhInputQDesc->get_data_type());
  queueObj.set_by_val<std::string>("queue_type", dlhInputQDesc->get_queue_type());
  queueObj.set_by_val<uint32_t>("capacity", dlhInputQDesc->get_capacity());
  queueObj.set_by_val<uint32_t>("source_id", id);

  auto faServiceObj = dlhReqInputNetDesc->get_associated_service()->config_object();
  std::string faNetUid = dlhReqInputNetDesc->get_uid_base() + UID();
  conffwk::ConfigObject faNetObj;
  confdb->create(dbfile, "NetworkConnection", faNetUid, faNetObj);
  faNetObj.set_by_val<std::string>("connection_type", dlhReqInputNetDesc->get_connection_type());
  faNetObj.set_by_val<std::string>("data_type", dlhReqInputNetDesc->get_data_type());
  faNetObj.set_obj("associated_service", &faServiceObj);

  dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

  modules.push_back(confdb->get<DataHandlerModule>(uid));

  auto hsiServiceObj = hsiNetDesc->get_associated_service()->config_object();
  std::string hsiNetUid = hsiNetDesc->get_uid_base();
  conffwk::ConfigObject hsiNetObj;
  confdb->create(dbfile, "NetworkConnection", hsiNetUid, hsiNetObj);
  hsiNetObj.set_by_val<std::string>("connection_type", hsiNetDesc->get_connection_type());
  hsiNetObj.set_by_val<std::string>("data_type", hsiNetDesc->get_data_type());
  hsiNetObj.set_obj("associated_service", &hsiServiceObj);
  
  std::string genuid("HSI-" + std::to_string(id));
  conffwk::ConfigObject hsiObj;
  confdb->create(dbfile, "HSIReadout", genuid, hsiObj);
  hsiObj.set_obj("configuration", &rdrConf->config_object());
  hsiObj.set_objs("outputs", { &queueObj, &hsiNetObj });

  modules.push_back(confdb->get<HSIReadout>(genuid));

  return modules;
}
