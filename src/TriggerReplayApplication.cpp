/**
 * @file generate_modules.cpp
 *
 * Implementation of TriggerReplayApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "conffwk/Configuration.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"

#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"

#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"

#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/QueueDescriptor.hpp"

#include "appmodel/SourceIDConf.hpp"

#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataSubscriberModule.hpp"

#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/TCDataProcessor.hpp"

#include "appmodel/TriggerPrimitiveMakerModule.hpp"
#include "appmodel/TriggerPrimitiveMakerModuleConf.hpp"

#include "appmodel/TriggerReplayApplication.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("TriggerReplayApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             conffwk::Configuration* confdb,
                                             const std::string& dbfile,
                                             const confmodel::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<TriggerReplayApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

std::vector<const confmodel::DaqModule*>
TriggerReplayApplication::generate_modules(conffwk::Configuration* confdb,
                                 const std::string& dbfile,
                                 const confmodel::Session* /*session*/) const
{

  /***** MODULES *****/

  std::vector<const confmodel::DaqModule*> modules;

  /**************************************************************
   * Instantiate the Trigger Primitive Maker Module module
   **************************************************************/

  auto tpmm_conf = get_tpmm_conf();

  if (!tpmm_conf) {
    throw(BadConf(ERS_HERE, "No Replay configuration in TriggerReplayApplication given"));
  }

  conffwk::ConfigObject tpm_obj;
  confdb->create(dbfile, tpmm_conf->get_template_for(), tpmm_conf->UID(), tpm_obj);
  tpm_obj.set_obj("configuration", &(tpmm_conf->config_object()));

  /**************************************************************
   * Instantiate the TP Handler (TA Maker) module
   **************************************************************/
  auto tph_conf = get_tp_handler();
  std::string tph_class = "";
  if (tph_conf != nullptr) {
    tph_class = tph_conf->get_template_for();
  }

  auto tph_conf_obj = tph_conf->config_object();
  conffwk::ConfigObject tph_obj;
  std::string tp_uid("tphandler-replay-1");
  confdb->create(dbfile, tph_class, tp_uid, tph_obj);
  tph_obj.set_by_val<uint32_t>("source_id", 0);
  tph_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  tph_obj.set_by_val<bool>("post_processing_enabled", true);
  tph_obj.set_obj("module_configuration", &tph_conf_obj);

  auto tph2_conf_obj = tph_conf->config_object();
  conffwk::ConfigObject tph2_obj;
  std::string tp2_uid("tphandler-replay-2");
  confdb->create(dbfile, tph_class, tp2_uid, tph2_obj);
  tph2_obj.set_by_val<uint32_t>("source_id", 1);
  tph2_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  tph2_obj.set_by_val<bool>("post_processing_enabled", true);
  tph2_obj.set_obj("module_configuration", &tph2_conf_obj);

  auto tph3_conf_obj = tph_conf->config_object();
  conffwk::ConfigObject tph3_obj;
  std::string tp3_uid("tphandler-replay-3");
  confdb->create(dbfile, tph_class, tp3_uid, tph3_obj);
  tph3_obj.set_by_val<uint32_t>("source_id", 2);
  tph3_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  tph3_obj.set_by_val<bool>("post_processing_enabled", true);
  tph3_obj.set_obj("module_configuration", &tph3_conf_obj);

  auto tph4_conf_obj = tph_conf->config_object();
  conffwk::ConfigObject tph4_obj;
  std::string tp4_uid("tphandler-replay-4");
  confdb->create(dbfile, tph_class, tp4_uid, tph4_obj);
  tph4_obj.set_by_val<uint32_t>("source_id", 3);
  tph4_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  tph4_obj.set_by_val<bool>("post_processing_enabled", true);
  tph4_obj.set_obj("module_configuration", &tph_conf_obj);

  /**************************************************************
  * Deal with queues
  **************************************************************/
  // load queue confs
  const QueueDescriptor* tp_inputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "TriggerDataHandlerModule" && data_type == "TriggerPrimitiveVector") {
      tp_inputq_desc = rule->get_descriptor();
    }
  }

  // setup queue objects
  conffwk::ConfigObject tp_iq1_obj;
  std::string tp_iq1_uid("tpinput-1");
  confdb->create(dbfile, "QueueWithSourceId", tp_iq1_uid, tp_iq1_obj);
  tp_iq1_obj.set_by_val<std::string>("data_type", tp_inputq_desc->get_data_type());
  tp_iq1_obj.set_by_val<std::string>("queue_type", tp_inputq_desc->get_queue_type());
  tp_iq1_obj.set_by_val<uint32_t>("capacity", tp_inputq_desc->get_capacity());
  tp_iq1_obj.set_by_val<uint32_t>("source_id", 0);

  /**************************************************************
  * Deal with network connections
  **************************************************************/
  const NetworkConnectionDescriptor* ta_net_desc = nullptr;
  const NetworkConnectionDescriptor* dr_net_desc = nullptr;
  
  for (auto rule : get_network_rules()) {  
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (data_type == "TriggerActivity") {
      ta_net_desc = rule->get_descriptor();
    }
    else if (data_type == "DataRequest") {
      dr_net_desc = rule->get_descriptor(); 
    }
  }

  conffwk::ConfigObject ta_net_obj;
  auto ta_service_obj = ta_net_desc->get_associated_service()->config_object();
  std::string ta_stream_uid(ta_net_desc->get_uid_base()+UID());
  confdb->create(dbfile, "NetworkConnection", ta_stream_uid, ta_net_obj);
  ta_net_obj.set_by_val<std::string>("data_type", ta_net_desc->get_data_type());
  ta_net_obj.set_by_val<std::string>("connection_type", ta_net_desc->get_connection_type());
  ta_net_obj.set_obj("associated_service", &ta_service_obj);

  conffwk::ConfigObject dr_net_obj;
  auto dr_service_obj = dr_net_desc->get_associated_service()->config_object();
  std::string dr_stream_uid(dr_net_desc->get_uid_base()+UID());
  confdb->create(dbfile, "NetworkConnection", dr_stream_uid, dr_net_obj);
  dr_net_obj.set_by_val<std::string>("data_type", dr_net_desc->get_data_type());
  dr_net_obj.set_by_val<std::string>("connection_type", dr_net_desc->get_connection_type());
  dr_net_obj.set_obj("associated_service", &dr_service_obj);
 
  /**************************************************************
  * Finally set inputs & outputs
  **************************************************************/

  tpm_obj.set_objs("outputs", {&tp_iq1_obj} );
  //tpm_obj.set_objs("outputs", {&tp_iq1_obj, &tp_iq2_obj, &tp_iq3_obj, &tp_iq4_obj} ); 
  tph_obj.set_objs("inputs", {&tp_iq1_obj, &dr_net_obj} );
  tph_obj.set_objs("outputs", {&ta_net_obj} );

  // store modules
  modules.push_back(confdb->get<confmodel::DaqModule>(tpmm_conf->UID()));
  modules.push_back(confdb->get<confmodel::DaqModule>(tp_uid));
  //modules.push_back(confdb->get<confmodel::DaqModule>(tp2_uid));
  //modules.push_back(confdb->get<confmodel::DaqModule>(tp3_uid));
  //modules.push_back(confdb->get<confmodel::DaqModule>(tp4_uid));

  return modules;
}
