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

  // For now, have X identical config TP Handlers
  // X = either num of files if < 4; or 4
  // Later, do this dynamically, but that requires opening the HDF5s...
  int max_APAs = tpmm_conf->get_tp_streams().size();
  int APA_limit = std::min(max_APAs, 4);
  std::vector<std::shared_ptr<conffwk::ConfigObject>> TPHs;
  std::vector<std::string> TPHs_uids;

  auto tph_conf_obj = tph_conf->config_object();
  for (int i = 1; i <= APA_limit; i++) {
    auto tph_obj = std::make_shared<conffwk::ConfigObject>();
    std::string tp_uid = "tphandler-replay-" + std::to_string(i);
    TPHs_uids.push_back(tp_uid);
    confdb->create(dbfile, tph_class, tp_uid, *tph_obj);
    tph_obj->set_by_val<uint32_t>("source_id", i);
    tph_obj->set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
    tph_obj->set_by_val<bool>("post_processing_enabled", true);
    tph_obj->set_obj("module_configuration", &tph_conf_obj);
    TPHs.push_back(tph_obj);
  }

  /**************************************************************
   * Deal with queues
   **************************************************************/
  // Load queue configurations
  const QueueDescriptor* tp_inputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "TriggerDataHandlerModule" && data_type == "TriggerPrimitiveVector") {
      tp_inputq_desc = rule->get_descriptor();
    }
  }

  // Same as above, later dynamically
  std::vector<std::shared_ptr<conffwk::ConfigObject>> TP_queues;
  for (int i = 1; i <= APA_limit; i++) {
    auto tp_q_obj = std::make_shared<conffwk::ConfigObject>();
    std::string tp_q_uid = "tpinput-" + std::to_string(i);
    confdb->create(dbfile, "QueueWithSourceId", tp_q_uid, *tp_q_obj);
    tp_q_obj->set_by_val<std::string>("data_type", tp_inputq_desc->get_data_type());
    tp_q_obj->set_by_val<std::string>("queue_type", tp_inputq_desc->get_queue_type());
    tp_q_obj->set_by_val<uint32_t>("capacity", tp_inputq_desc->get_capacity());
    tp_q_obj->set_by_val<uint32_t>("source_id", i);
    TP_queues.push_back(tp_q_obj);
  }

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
    } else if (data_type == "DataRequest") {
      dr_net_desc = rule->get_descriptor();
    }
  }

  // Create vectors for network connections
  std::vector<std::shared_ptr<conffwk::ConfigObject>> ta_net_objects;
  std::vector<std::shared_ptr<conffwk::ConfigObject>> dr_net_objects;

  for (int i = 1; i <= APA_limit; i++) {
    auto ta_net_obj = std::make_shared<conffwk::ConfigObject>();
    auto ta_service_obj = ta_net_desc->get_associated_service()->config_object();
    std::string ta_stream_uid = ta_net_desc->get_uid_base() + UID() + "-" + std::to_string(i);
    confdb->create(dbfile, "NetworkConnection", ta_stream_uid, *ta_net_obj);
    ta_net_obj->set_by_val<std::string>("data_type", ta_net_desc->get_data_type());
    ta_net_obj->set_by_val<std::string>("connection_type", ta_net_desc->get_connection_type());
    ta_net_obj->set_obj("associated_service", &ta_service_obj);
    ta_net_objects.push_back(ta_net_obj);

    auto dr_net_obj = std::make_shared<conffwk::ConfigObject>();
    auto dr_service_obj = dr_net_desc->get_associated_service()->config_object();
    std::string dr_stream_uid = dr_net_desc->get_uid_base() + UID() + "-" + std::to_string(i);
    confdb->create(dbfile, "NetworkConnection", dr_stream_uid, *dr_net_obj);
    dr_net_obj->set_by_val<std::string>("data_type", dr_net_desc->get_data_type());
    dr_net_obj->set_by_val<std::string>("connection_type", dr_net_desc->get_connection_type());
    dr_net_obj->set_obj("associated_service", &dr_service_obj);
    dr_net_objects.push_back(dr_net_obj);
  }

  /**************************************************************
   * Finally set inputs & outputs
   **************************************************************/
  // Convert TP_queues to a vector of raw pointers
  std::vector<const conffwk::ConfigObject*> raw_tp_queues;
  for (const auto& tp_queue : TP_queues) {
    raw_tp_queues.push_back(tp_queue.get());
  }
  tpm_obj.set_objs("outputs", raw_tp_queues);

  for (int i = 1; i <= APA_limit; i++) {
    // Convert network objects to raw pointers
    std::vector<const conffwk::ConfigObject*> temp_inputs = {
      TP_queues[i - 1].get(),
      dr_net_objects[i - 1].get()
    };
    TPHs[i - 1]->set_objs("inputs", temp_inputs);
    TPHs[i - 1]->set_objs("outputs", { ta_net_objects[i - 1].get() });
  }

  // Store modules
  modules.push_back(confdb->get<confmodel::DaqModule>(tpmm_conf->UID()));
  for (int i = 1; i <= APA_limit; i++) {
    modules.push_back(confdb->get<confmodel::DaqModule>(TPHs_uids[i - 1]));
  }

  return modules;
}
