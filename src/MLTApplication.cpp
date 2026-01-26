/**
 * @file MLTApplication.cpp
 *
 * Implementation of MLTApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"

#include "appmodel/ConfigurationHelper.hpp"
#include "conffwk/Configuration.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
// #include "confmodel/ReadoutGroup.hpp"
// #include "confmodel/ReadoutInterface.hpp"
// #include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"

#include "appmodel/DTSHSIApplication.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataRecorderConf.hpp"
#include "appmodel/DataSubscriberModule.hpp"
#include "appmodel/CTBApplication.hpp"
#include "appmodel/FakeDataApplication.hpp"
#include "appmodel/FakeDataProdConf.hpp"
#include "appmodel/FakeHSIApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/MLTConf.hpp"
#include "appmodel/MLTModule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "appmodel/StandaloneTCMakerConf.hpp"
#include "appmodel/StandaloneTCMakerModule.hpp"
#include "appmodel/TCDataProcessor.hpp"
#include "appmodel/TPStreamConf.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/TPReplayModuleConf.hpp"
#include "appmodel/TPReplayApplication.hpp"
#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {


void
MLTApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);

  // auto mlt_conf = get_mlt_conf();
  // auto mlt_class = mlt_conf->get_template_for();

  auto tch_conf = get_trigger_inputs_handler();
  auto tch_class = tch_conf->get_template_for();

  auto mlt_conf = get_mlt_conf();
  auto mlt_class = mlt_conf->get_template_for();
  std::string handler_name(tch_conf->UID());

  if (!mlt_conf) {
    throw(BadConf(ERS_HERE, "No MLT configuration in MLTApplication given"));
  }

  // Queue descriptors
  // Process the queue rules looking for inputs to our trigger handler modules
  const QueueDescriptor* tc_inputq_desc = nullptr;
  const QueueDescriptor* td_outputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == tch_class) {
      tc_inputq_desc = rule->get_descriptor();
    } else if (destination_class == mlt_class) {
      td_outputq_desc = rule->get_descriptor();
    }
  }

  if (tc_inputq_desc == nullptr) {
    throw(BadConf(ERS_HERE, "No TC input queue descriptor given"));
  }
  if (td_outputq_desc == nullptr) {
    throw(BadConf(ERS_HERE, "No TD output-input queue descriptor given"));
  }

  // Create queues
  auto input_queue_obj = obj_fac.create_queue_obj(tc_inputq_desc);
  auto output_queue_obj = obj_fac.create_queue_obj(td_outputq_desc);

  // Net descriptors
  const NetworkConnectionDescriptor* req_net_desc = nullptr;
  const NetworkConnectionDescriptor* tc_net_desc = nullptr;
  const NetworkConnectionDescriptor* ti_net_desc = nullptr;
  const NetworkConnectionDescriptor* td_net_desc = nullptr;
  const NetworkConnectionDescriptor* timesync_net_desc = nullptr;

  for (auto rule : get_network_rules()) {
    std::string data_type = rule->get_descriptor()->get_data_type();

    // Network connections for the MLT
    if (data_type == "TriggerInhibit") {
      ti_net_desc = rule->get_descriptor();
    }
    if (data_type == "TriggerDecision") {
      td_net_desc = rule->get_descriptor();
    }
    if (data_type == "TriggerCandidate") {
      tc_net_desc = rule->get_descriptor();
    }
    if (data_type == "TimeSync") {
      timesync_net_desc = rule->get_descriptor();
    }
    if (data_type == "DataRequest") {
      req_net_desc = rule->get_descriptor();
    }

    TLOG_DEBUG(3) << "Endpoint class (currently not used in for networkconnections): data_type: " << data_type;
  }

  if (!td_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerDecision given"));
  }
  if (!ti_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the output TriggerInhibit given"));
  }
  if (!tc_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the Input of TriggerCandidates given"));
  }
  if (!req_net_desc) {
    throw(BadConf(ERS_HERE, "No MLT network connection for the Input of DataRequests given"));
  }
  // Network connection for input TriggerInhibit, input TCs

  conffwk::ConfigObject ti_net_obj =
    obj_fac.create_net_obj(ti_net_desc, "");

  conffwk::ConfigObject tc_net_obj =
    obj_fac.create_net_obj(tc_net_desc, ".*");

  // Network connection for output TriggerDecision
  conffwk::ConfigObject td_net_obj =
    obj_fac.create_net_obj(td_net_desc, "");

  // Network conection for the input Data Requests
  conffwk::ConfigObject dr_net_obj =
    obj_fac.create_net_obj(req_net_desc, UID());

  conffwk::ConfigObject timesync_net_obj;
  if (timesync_net_desc != nullptr) {
    timesync_net_obj =
      obj_fac.create_net_obj(timesync_net_desc, ".*");
  }

  /**************************************************************
   * Instantiate standalone TC generator modules (e.g. random TC generator)
   **************************************************************/

  auto standalone_TC_maker_confs = get_standalone_candidate_maker_confs();
  std::vector<conffwk::ConfigObject> generated_tc_conns;
  generated_tc_conns.reserve(standalone_TC_maker_confs.size());
  for (auto gen_conf : standalone_TC_maker_confs) {
    conffwk::ConfigObject gen_obj = obj_fac.create(gen_conf->get_template_for(),
                                                   gen_conf->UID());
    gen_obj.set_obj("configuration", &(gen_conf->config_object()));
    if (gen_conf->get_timestamp_method() == "kTimeSync" && !timesync_net_obj.is_null()) {
      gen_obj.set_objs("inputs", { &timesync_net_obj });
    }

    auto tc_net_gen = obj_fac.create_net_obj(tc_net_desc, gen_conf->UID());
    generated_tc_conns.push_back(tc_net_gen);

    gen_obj.set_objs("outputs", { &generated_tc_conns.back() });
    modules.push_back(obj_fac.get_dal<StandaloneTCMakerModule>(gen_conf->UID()));
  }

  /**************************************************************
   * Create the Data Reader
   **************************************************************/
  auto rdr_conf = get_data_subscriber();
  if (rdr_conf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataReaderModule configuration given"));
  }

  std::string reader_uid("data-reader-" + UID());
  std::string reader_class = rdr_conf->get_template_for();
  TLOG_DEBUG(7) << "creating OKS configuration object for Data subscriber class " << reader_class;
  conffwk::ConfigObject reader_obj = obj_fac.create(reader_class, reader_uid);
  reader_obj.set_objs("inputs", { &tc_net_obj });
  reader_obj.set_objs("outputs", { &input_queue_obj });
  reader_obj.set_obj("configuration", &rdr_conf->config_object());

  modules.push_back(obj_fac.get_dal<DataSubscriberModule>(reader_uid));

  /**************************************************************
   * Create the readout map
   **************************************************************/

  std::vector<const conffwk::ConfigObject*> sourceIds;
  for (auto [uid, source_ids]: helper->get_stream_source_ids()) {
    for (auto src_id: source_ids) {
      // Create SourceIDConf object for the MLT
      std::string sourceIdConfUID = "dro-mlt-stream-config-" +
        std::to_string(src_id);
      conffwk::ConfigObject* sourceIdConf = new conffwk::ConfigObject(
        obj_fac.create("SourceIDConf", sourceIdConfUID));
      sourceIdConf->set_by_val<uint32_t>("sid", src_id);
      // https://github.com/DUNE-DAQ/daqdataformats/blob/5b99506675a586c8a09123900e224f2371d96df9/include/daqdataformats/detail/SourceID.hxx#L108
      sourceIdConf->set_by_val<std::string>("subsystem", "Detector_Readout");
      sourceIds.push_back(sourceIdConf);
    }
  }
  for (auto [uid, source_ids]: helper->get_tp_source_ids()) {
    for (auto src_id: source_ids) {
      sourceIds.push_back(&(src_id->config_object()));
    }
  }

  for (auto app_class: {"TriggerApplication", "FakeHSIApplication",
      "DTSHSIApplication", "CTBApplication"}) {
    for (auto [uid, src_id]: helper->get_app_source_ids(app_class)) {
      auto tcSourceIdConf = new conffwk::ConfigObject(
        obj_fac.create("SourceIDConf",
                       uid + "-" + std::to_string(src_id->get_sid())
          ));
      tcSourceIdConf->set_by_val<uint32_t>("sid", src_id->get_sid());
      tcSourceIdConf->set_by_val<std::string>("subsystem", src_id->get_subsystem());
      sourceIds.push_back(tcSourceIdConf);
    }    
  }
  
  // Get mandatory links
  std::vector<const conffwk::ConfigObject*> mandatory_sids;
  const TCDataProcessor* tc_dp = tch_conf->get_data_processor()->cast<TCDataProcessor>();
  if (tc_dp != nullptr) {
    for (auto m : tc_dp->get_mandatory_links()) {
      mandatory_sids.push_back(&m->config_object());
    }
  }

  /**************************************************************
   * Create the TC handler
   **************************************************************/

  // Process special Network rules!
  // Looking for Fragment rules from DFAppplications in current Session

  // auto sessionApps = session->get_enabled_applications();
  // std::vector<conffwk::ConfigObject> fragOutObjs;
  // for (auto app : sessionApps) {
  //   auto dfapp = app->cast<appmodel::DFApplication>();
  //   if (dfapp == nullptr)
  //     continue;

  //   auto dfNRules = dfapp->get_network_rules();
  //   for (auto rule : dfNRules) {
  //     auto descriptor = rule->get_descriptor();
  //     auto data_type = descriptor->get_data_type();
  //     if (data_type == "Fragment") {
  //       std::string dreqNetUid(descriptor->get_uid_base() + dfapp->UID());
  //       conffwk::ConfigObject frag_conn;
  //       confdb->create(dbfile, "NetworkConnection", dreqNetUid, frag_conn);

  //       frag_conn.set_by_val<std::string>("data_type", descriptor->get_data_type());
  //       frag_conn.set_by_val<std::string>("connection_type", descriptor->get_connection_type());

  //       auto serviceObj = descriptor->get_associated_service()->config_object();
  //       frag_conn.set_obj("associated_service", &serviceObj);
  //       fragOutObjs.push_back(frag_conn);
  //     } // If network rule has TriggerDecision type of data
  //   }   // Loop over Apps network rules
  // }     // loop over Session specific Apps

  std::vector<conffwk::ConfigObject> fragOutObjs;
  for (auto [uid, descriptor]:
         helper->get_netdescriptors("Fragment", "DFApplication")) {
    fragOutObjs.emplace_back(obj_fac.create_net_obj(descriptor, uid));
  }    

  // build up the full list of outputs
  std::vector<const conffwk::ConfigObject*> ti_output_objs;
  for (auto& fNet : fragOutObjs) {
    ti_output_objs.push_back(&fNet);
  }
  ti_output_objs.push_back(&output_queue_obj);

  auto tch_conf_obj = tch_conf->config_object();
  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "No source_id associated with this TriggerApplication!"));
  }
  uint32_t source_id = get_source_id()->get_sid();
  std::string ti_uid(handler_name + "-" + std::to_string(source_id));
  conffwk::ConfigObject ti_obj = obj_fac.create(tch_class, ti_uid);
  ti_obj.set_by_val<uint32_t>("source_id", source_id);
  ti_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  ti_obj.set_obj("module_configuration", &tch_conf_obj);
  ti_obj.set_objs("enabled_source_ids", sourceIds);
  ti_obj.set_objs("mandatory_source_ids", mandatory_sids);
  ti_obj.set_objs("inputs", { &input_queue_obj, &dr_net_obj });
  ti_obj.set_objs("outputs", ti_output_objs);

  // Add to our list of modules to return
  modules.push_back(obj_fac.get_dal<DataHandlerModule>(ti_uid));

  /**************************************************************
   * Instantiate the MLTModule module
   **************************************************************/

  conffwk::ConfigObject mlt_obj = obj_fac.create(mlt_conf->get_template_for(),
                                                 mlt_conf->UID());
  mlt_obj.set_obj("configuration", &(mlt_conf->config_object()));
  mlt_obj.set_objs("inputs", { &output_queue_obj, &ti_net_obj });
  mlt_obj.set_objs("outputs", { &td_net_obj });
  modules.push_back(obj_fac.get_dal<MLTModule>(mlt_conf->UID()));

  obj_fac.update_modules(modules);
}
 
} // namespace appmodel  
} // namespace dunedaq
