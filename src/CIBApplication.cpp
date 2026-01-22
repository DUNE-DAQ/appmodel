/**
 * @file CIBApplication.cpp
 *
 * Implementation of CIBApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "conffwk/Configuration.hpp"
#include "oks/kernel.hpp"
#include "logging/Logging.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/DetDataSender.hpp"

#include "ConfigObjectFactory.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "appmodel/CIBApplication.hpp"

#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataHandlerModule.hpp"

#include <string>
#include <vector>
#include <bitset>
#include <iostream>
#include <fmt/core.h>
#include <set>

using namespace dunedaq;
using namespace dunedaq::appmodel;

void
CIBApplication::generate_modules(const confmodel::Session* session) const
{
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);
  
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

  
  auto cib_conf = get_cib_conf();
  if (cib_conf ==nullptr) {
    throw(BadConf(ERS_HERE, "No CIBModule configuration given"));
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
	std::string dreqNetUid(descriptor->get_uid_base() + dfapp->UID());
	conffwk::ConfigObject frag_conn = obj_fac.create_net_obj(descriptor, dreqNetUid);
	fragOutObjs.push_back(frag_conn);
      } // If network rule has TriggerDecision type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps
  
  // start building the list of outputs
  std::vector<const conffwk::ConfigObject*> fh_output_objs;
  for (auto& fNet : fragOutObjs) {
    fh_output_objs.push_back(&fNet);
  }

  std::vector<conffwk::ConfigObject> cib_module_outputs;
  auto source = get_source_id();
  if (source == nullptr) {
    throw(BadConf(ERS_HERE, "No SourceIDConf given for CIBApplication"));
  }

  auto id = source->get_sid();

  // ----------------------------
  // create DLH
  // ----------------------------
  auto det_id = 1; // TODO Eric Flumerfelt <eflumerf@fnal.gov>, 08-Feb-2024: This is a magic number corresponding to kDAQ
  TLOG() << "creating OKS configuration object for Data Link Handler class " << dlhClass << ", id " << id;
  std::string uid("DLH-");
  conffwk::ConfigObject dlhObj = obj_fac.create( dlhClass, uid );
  dlhObj.set_by_val<uint32_t>("source_id", id);
  dlhObj.set_by_val<uint32_t>("detector_id", det_id);
  dlhObj.set_by_val<bool>("post_processing_enabled", false);
  dlhObj.set_obj("module_configuration", &dlhConf->config_object());

  auto net_objc(fh_output_objs);

  // Time Sync network connection
  if (dlhConf->get_generate_timesync()) {
    std::string tsStreamUid = tsNetDesc->get_uid_base() + std::to_string(id);
    conffwk::ConfigObject tsNetObj = obj_fac.create_net_obj(tsNetDesc, tsStreamUid);
    net_objc.push_back(&tsNetObj);
  }

  dlhObj.set_objs("outputs", net_objc);

  // create Queues from CIB to DLH
  std::string dataQueueUid(dlhInputQDesc->get_uid_base());
  conffwk::ConfigObject queueObj = obj_fac.create_queue_sid_obj(dlhInputQDesc, id);
  queueObj.rename(dataQueueUid);

  cib_module_outputs.push_back(queueObj);

  // Create network connections to DLHs
  std::string faNetUid = dlhReqInputNetDesc->get_uid_base() + UID();
  conffwk::ConfigObject faNetObj = obj_fac.create_net_obj(dlhReqInputNetDesc, faNetUid);

  dlhObj.set_objs("inputs", { &queueObj, &faNetObj });

  modules.push_back(obj_fac.get_dal<appmodel::DataHandlerModule>(uid));


  // HSI and BoardConfs
  conffwk::ConfigObject hsiNetObj = obj_fac.create_net_obj(hsiNetDesc, "");
  cib_module_outputs.push_back(hsiNetObj);

  // auto board = get_board();

  // conffwk::ConfigObject module_obj = obj_fac.create( "CIBModule", "cib-module");
  // module_obj.set_obj("configuration", & cib_conf -> config_object() );
  // module_obj.set_obj("board", & board -> config_object() );

  // std::vector<const conffwk::ConfigObject*> cib_module_output_ptrs;
  // for ( const auto & o : cib_module_outputs ) {
  //   cib_module_output_ptrs.push_back( & o );
  // }

  // module_obj.set_objs("outputs", cib_module_output_ptrs);

  // auto module = obj_fac.get_dal<appmodel::CIBModule>(module_obj.UID());

  // modules.push_back(module);

  obj_fac.update_modules(modules);
}





