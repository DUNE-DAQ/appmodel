/**
 * @file generate_modules.cpp
 *
 * Implementation of NP02ReadoutApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ConfigObjectFactory.hpp"
#include "appmodel/NP02ReadoutApplication.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/NetworkDevice.hpp"
#include "confmodel/DetDataSender.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/NWDetDataReceiver.hpp"
#include "appmodel/NWDetDataSender.hpp"
#include "appmodel/DPDKReceiver.hpp"

#include "appmodel/FelixDataReceiver.hpp"
#include "appmodel/FelixDataSender.hpp"

#include "appmodel/SocketReceiver.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"

#include "appmodel/SourceIDConf.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
#include "appmodel/DataReaderModule.hpp"
#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataRecorderModule.hpp"
#include "appmodel/DataRecorderConf.hpp"

#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/FragmentAggregatorModule.hpp"
#include "appmodel/FragmentAggregatorConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/RequestHandler.hpp"
#include "appmodel/LatencyBuffer.hpp"
#include "appmodel/DataProcessor.hpp"


#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"
#include <fmt/core.h>

#include <string>
#include <vector>

// using namespace dunedaq;
// using namespace dunedaq::appmodel;

namespace dunedaq {
namespace appmodel {

//-----------------------------------------------------------------------------
void
NP02ReadoutApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{

  TLOG_DEBUG(6) << "Generating modules for application " << this->UID();

  ConfigObjectFactory obj_fac(this);

  //
  // Extract basic configuration objects
  //

  // Data reader
  auto reader_conf = get_data_reader();
  if (reader_conf == 0) {
    throw(BadConf(ERS_HERE, "No DataReaderModule configuration given"));
  }
  std::string reader_class = reader_conf->get_template_for();

  // Link handler
  auto dlh_conf = get_link_handler();
  // What is template for?
  auto dlh_class = dlh_conf->get_template_for();

  auto tph_conf = get_tp_handler();
  if (tph_conf==nullptr && get_tp_generation_enabled()) {
    throw(BadConf(ERS_HERE, "TP generation is enabled but there is no TP data handler configuration"));
  }

  std::string tph_class = "";
  if (tph_conf != nullptr && get_tp_generation_enabled()) {
    tph_class = tph_conf->get_template_for();
  }

  //
  // Process the queue rules looking for inputs to our DL/TP handler modules
  //
  const QueueDescriptor* dlh_reqinput_qdesc = nullptr;
  const QueueDescriptor* tp_input_qdesc = nullptr;
  // const QueueDescriptor* tpReqInputQDesc = nullptr;
  const QueueDescriptor* fa_output_qdesc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    // Why datahander here?
    if (destination_class == "DataHandlerModule" || destination_class == dlh_class || destination_class == tph_class) {
      if (data_type == "DataRequest") {
        dlh_reqinput_qdesc = rule->get_descriptor();
      } else if ((data_type == "TriggerPrimitive" || data_type == "TriggerPrimitiveVector") && get_tp_generation_enabled()) {
        tp_input_qdesc = rule->get_descriptor();
      }
    } else if (destination_class == "FragmentAggregatorModule") {
      fa_output_qdesc = rule->get_descriptor();
    }
  }

  //
  // Process the network rules looking for the Fragment Aggregator and TP handler data reuest inputs
  //
  const NetworkConnectionDescriptor* fa_net_desc = nullptr;
  const NetworkConnectionDescriptor* tp_net_desc = nullptr;
  const NetworkConnectionDescriptor* ta_net_desc = nullptr;
  const NetworkConnectionDescriptor* ts_net_desc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();

    if (endpoint_class == "FragmentAggregatorModule") {
      fa_net_desc = rule->get_descriptor();
    } else if (data_type == "TPSet") {
      tp_net_desc = rule->get_descriptor();
    } else if (data_type == "TriggerActivity") {
      ta_net_desc = rule->get_descriptor();
    } else if (data_type == "TimeSync") {
      ts_net_desc = rule->get_descriptor();
    }
  }

  // Create here the Queue on which all data fragments are forwarded to the fragment aggregator
  // and a container for the queues of data request to TP handler and DLH
  if (fa_output_qdesc == nullptr) {
    throw(BadConf(ERS_HERE, "No fragment output queue descriptor given"));
  }
  std::vector<const confmodel::Connection*> req_queues;
  conffwk::ConfigObject frag_queue_obj = obj_fac.create_queue_obj(fa_output_qdesc);

  //
  // Get the callback descriptor
  //
  const DataMoveCallbackDescriptor* raw_data_callback_desc = get_callback_desc();

  if (raw_data_callback_desc == nullptr) {
    throw(BadConf(ERS_HERE, "No Raw Data Callback descriptor given"));
  }

  //
  // Scan Detector 2 DAQ connections to extract sender, receiver and stream information
  //

  std::vector<const confmodel::DaqModule*> modules;

  // Loop over the detector to daq connections and generate one data reader per connection
  // and the cooresponding datalink handlers

  // Collect all streams
  std::vector<std::pair<int16_t, const confmodel::DetectorStream*>> all_enabled_det_streams;
  std::map<uint32_t, const appmodel::DataMoveCallbackConf*> callback_confs_by_sid;

  std::vector<const conffwk::ConfigObject*> d2d_conn_objs;
  uint16_t conn_idx = 0;


  std::set<int16_t> numas;
  for (auto d2d_conn : get_detector_connections()) {
    uint16_t receiver_numa = 0;

    // Are we sure?
    if (helper->is_disabled(d2d_conn)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn->UID();
      continue;
    }

    d2d_conn_objs.push_back(&d2d_conn->config_object());

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module

    if (d2d_conn->senders().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain sebders or receivers"));
    }
    if (d2d_conn->receiver() == nullptr) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain a receiver"));
    }

    // Loop over detector 2 daq connections to find senders and receivers
    auto det_senders = d2d_conn->senders();
    auto det_receiver = d2d_conn->receiver();

    // Here I want to resolve the type of connection (network, felix, or?)
    // Rules of engagement: if the receiver interface is network or felix, the receivers should be castable to the counterpart
    bool requires_dpdk = (reader_class == "DPDKReaderModule" || reader_class == "FDFakeReaderModule");

    if (reader_class == "DPDKReaderModule" || reader_class == "SocketReaderModule" || reader_class == "FDFakeReaderModule") {
      if ((requires_dpdk && !det_receiver->cast<appmodel::DPDKReceiver>()) || // SSB: Note here, we are intrinsically locking FakeCard readout to only emulate DPDK data reception. Given NP02ReadoutApplication is intended for TDE readout at NP02, assuming this is OK.
          (reader_class == "SocketReaderModule" && !det_receiver->cast<appmodel::SocketReceiver>())) {
        std::string required_class = requires_dpdk ? "DPDKReceiver" : "SocketReceiver";
        throw(BadConf(ERS_HERE, fmt::format("{} requires {}, found {} of class {}", reader_class, required_class, det_receiver->UID(), det_receiver->class_name())));
      }

      // SSB: Note that here you need to include FDFakeCardReader as well, because emulated readout needs some way to map NUMA to streams
      // Since we require a receiver in the NetworkDetector2DAQConnections this would still work if the receiver type is a DPDKReceiver
      if (reader_class == "DPDKReaderModule" || reader_class == "FDFakeReaderModule") {
        auto dpdk_reciever = det_receiver->cast<appmodel::DPDKReceiver>();
        receiver_numa = (int16_t)dpdk_reciever->get_uses()->get_numa_id();
        TLOG_DEBUG(7) << "receiver numa: " << receiver_numa;
      }

      bool all_nw_senders = true;
      for (auto s : det_senders) {
        all_nw_senders &= (s->cast<appmodel::NWDetDataSender>() != nullptr);
      }

      // Ensure that all senders are compatible with receiver
      if (!all_nw_senders) {
        throw(BadConf(ERS_HERE, "Non-network DetDataSener found with NWreceiver"));
      }
    }

    std::vector<const confmodel::DetectorStream*> enabled_det_streams;
    // Loop over senders
    for (auto stream : d2d_conn->streams()) {

      // Are we sure?
      if (helper->is_disabled(stream)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorStream " << stream->UID();
        continue;
      }

      // loop over streams
      all_enabled_det_streams.push_back(std::make_pair(receiver_numa, stream));
      enabled_det_streams.push_back(stream);
      numas.insert(receiver_numa);
    }

  }

  //-----------------------------------------------------------------
  //
  // Create DataReaderModule object
  //

  //
  // Instantiate DataReaderModule of type DPDKReaderModule
  //

   // Create the Data reader object

    std::string reader_uid(fmt::format("datareader-{}-{}", this->UID(), std::to_string(conn_idx++)));
    TLOG_DEBUG(6) << fmt::format("creating OKS configuration object for Data reader class {} with id {}", reader_class, reader_uid);
    auto reader_obj = obj_fac.create(reader_class, reader_uid);

    // Populate configuration and interfaces (leave output queues for later)
    reader_obj.set_obj("configuration", &reader_conf->config_object());
    reader_obj.set_objs("connections", d2d_conn_objs);

    // Create the raw data callbacks
    std::vector<const conffwk::ConfigObject*> raw_data_callback_objs;

    // Create data queues
    for (auto& [numa, ds] : all_enabled_det_streams) {
      conffwk::ConfigObject callback_obj = obj_fac.create_callback_sid_obj(raw_data_callback_desc, ds->get_source_id());
      const auto* callback_conf = obj_fac.get_dal<DataMoveCallbackConf>(callback_obj.UID());
      raw_data_callback_objs.push_back(&callback_conf->config_object());
      callback_confs_by_sid[ds->get_source_id()] = callback_conf;
    }

    reader_obj.set_objs("raw_data_callbacks", raw_data_callback_objs);

    modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(reader_obj.UID()));




  //-----------------------------------------------------------------
  //
  // Prepare the tp handlers and related queues
  //
  std::vector<std::pair<uint32_t, const confmodel::Connection*>> tp_queues;

  if (get_tp_generation_enabled()) {

    // Create TP handler object
    auto tph_conf_obj = tph_conf->config_object();
    auto tpsrc_ids = get_tp_source_ids();

    if ((tpsrc_ids.size() % 3) > 0) {
      throw(BadConf(ERS_HERE, fmt::format("number of TP source IDs must be a multiple of 3, current amount: {}", tpsrc_ids.size())));
    }

    for (auto sid : tpsrc_ids) {
      conffwk::ConfigObject tp_queue_obj;
      conffwk::ConfigObject tpreq_queue_obj;
      std::string tp_uid("tphandler-" + std::to_string(sid->get_sid()));
      auto tph_obj = obj_fac.create(tph_class, tp_uid);
      tph_obj.set_by_val<uint32_t>("source_id", sid->get_sid());
      tph_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
      tph_obj.set_by_val<bool>("post_processing_enabled", get_ta_generation_enabled());
      tph_obj.set_obj("module_configuration", &tph_conf_obj);

      // Create the TPs aggregator queue (from RawData Handlers to TP handlers)
      tp_queue_obj = obj_fac.create_queue_sid_obj(tp_input_qdesc, sid->get_sid());
      tp_queue_obj.set_by_val<uint32_t>("recv_timeout_ms", 50);
      tp_queue_obj.set_by_val<uint32_t>("send_timeout_ms", 1);

      tp_queues.push_back(std::make_pair(sid->get_sid(), obj_fac.get_dal<confmodel::Connection>(tp_queue_obj.UID())));
      // Create tp data requests queue from Fragment Aggregator
      tpreq_queue_obj = obj_fac.create_queue_sid_obj(dlh_reqinput_qdesc, sid->get_sid());
      req_queues.push_back(obj_fac.get_dal<confmodel::Connection>(tpreq_queue_obj.UID()));

      // Create the tp(set) publishing service
      conffwk::ConfigObject tp_net_obj = obj_fac.create_net_obj(tp_net_desc, tp_uid);

      // Create the ta(set) publishing service
      conffwk::ConfigObject ta_net_obj = obj_fac.create_net_obj(ta_net_desc, tp_uid);

      // Register queues with tp handler
      tph_obj.set_objs("inputs", { &tp_queue_obj, &tpreq_queue_obj });
      tph_obj.set_objs("outputs", { &tp_net_obj, &ta_net_obj, &frag_queue_obj });
      modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(tph_obj.UID()));
    }
  }

  // Add output queueus of tps
  std::vector<std::pair<uint32_t, const conffwk::ConfigObject*>> tp_queue_objs;
  for (auto q : tp_queues) {
    tp_queue_objs.push_back(std::make_pair(q.first, &q.second->config_object()));
  }

  //-----------------------------------------------------------------
  //
  // Create datalink handlers
  //
  // Recover the emulation flag

  auto lb_conf = dlh_conf->get_latency_buffer();

  std::map<int16_t, conffwk::ConfigObject> numa_dhlconf_map;
  for ( int16_t numa : numas ) {
    auto lb_confobj_numa = obj_fac.create(lb_conf->class_name(), fmt::format("{}-numa{}",lb_conf->UID(), numa));
    lb_confobj_numa.set_by_val<uint32_t>("size", lb_conf->get_size());
    lb_confobj_numa.set_by_val<bool>("numa_aware", lb_conf->get_numa_aware());
    lb_confobj_numa.set_by_val<int16_t>("numa_node", numa);
    lb_confobj_numa.set_by_val<bool>("intrinsic_allocator", lb_conf->get_intrinsic_allocator());
    lb_confobj_numa.set_by_val<uint32_t>("alignment_size", lb_conf->get_alignment_size());
    lb_confobj_numa.set_by_val<bool>("preallocation", lb_conf->get_preallocation());

    auto dhl_confobj_numa = obj_fac.create(dlh_conf->class_name(), fmt::format("{}-numa{}",dlh_conf->UID(), numa));
    dhl_confobj_numa.set_by_val<std::string>("template_for", dlh_conf->get_template_for());
    dhl_confobj_numa.set_by_val<std::string>("input_data_type", dlh_conf->get_input_data_type());
    dhl_confobj_numa.set_by_val<bool>("generate_timesync", dlh_conf->get_generate_timesync());
    dhl_confobj_numa.set_by_val<uint64_t>("post_processing_delay_ticks", dlh_conf->get_post_processing_delay_ticks());
    dhl_confobj_numa.set_by_val<std::string>("input_data_type", dlh_conf->get_input_data_type());
    dhl_confobj_numa.set_obj("request_handler", &dlh_conf->get_request_handler()->config_object());
    dhl_confobj_numa.set_obj("latency_buffer", &lb_confobj_numa);
    dhl_confobj_numa.set_obj("data_processor", &dlh_conf->get_data_processor()->config_object());


    numa_dhlconf_map[numa] = dhl_confobj_numa;

  }

  auto emulation_mode = reader_conf->get_emulation_mode();
  for (auto& [numa, ds] : all_enabled_det_streams) {
    uint32_t sid = ds->get_source_id();
    TLOG_DEBUG(6) << fmt::format("Processing stream {}, id {}, det id {}", ds->UID(), ds->get_source_id(), ds->get_geo_id()->get_detector_id());
    std::string uid(fmt::format("DLH-{}", sid));
    TLOG_DEBUG(6) << fmt::format("creating OKS configuration object for Data Link Handler class {}, if {}", dlh_class, sid);
    auto dlh_obj = obj_fac.create(dlh_class, uid);
    dlh_obj.set_by_val<uint32_t>("source_id", sid);
    dlh_obj.set_by_val<uint32_t>("detector_id", ds->get_geo_id()->get_detector_id());
    dlh_obj.set_by_val<bool>("post_processing_enabled", get_tp_generation_enabled());
    dlh_obj.set_by_val<bool>("emulation_mode", emulation_mode);
    dlh_obj.set_obj("geo_id", &ds->get_geo_id()->config_object());
    dlh_obj.set_obj("module_configuration", &numa_dhlconf_map[numa]);
    dlh_obj.set_obj("raw_data_callback", &callback_confs_by_sid[sid]->config_object());

    std::vector<const conffwk::ConfigObject*> dlh_ins, dlh_outs;

    // Create request queue
    conffwk::ConfigObject req_queue_obj = obj_fac.create_queue_sid_obj(dlh_reqinput_qdesc, ds);


    // Add the requessts queue dal pointer to the outputs of the FragmentAggregatorModule
    req_queues.push_back(obj_fac.get_dal<confmodel::Connection>(req_queue_obj.UID()));
    dlh_ins.push_back(&req_queue_obj);
    dlh_outs.push_back(&frag_queue_obj);


    // Time Sync network connection
    if (dlh_conf->get_generate_timesync()) {
      // Add timestamp endpoint
      conffwk::ConfigObject ts_net_obj = obj_fac.create_net_obj(ts_net_desc, std::to_string(sid));
      dlh_outs.push_back(&ts_net_obj);
    }

    // here, we want to select which tp queues to add to the output, to separate mutiple detector elements
    for (auto tpq : tp_queue_objs) {
        if ((sid / 100) == (tpq.first / 10)) {
        dlh_outs.push_back(tpq.second);
      }
    }
    dlh_obj.set_objs("inputs", dlh_ins);
    dlh_obj.set_objs("outputs", dlh_outs);

    modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(dlh_obj.UID()));
  }


  // Finally create Fragment Aggregator
  auto aggregator_conf = get_fragment_aggregator();
  if (aggregator_conf == 0) {
    throw(BadConf(ERS_HERE, "No FragmentAggregatorModule configuration given"));
  }
  std::string faUid("fragmentaggregator-" + UID());
  // conffwk::ConfigObject frag_aggr;
  TLOG_DEBUG(7) << "creating OKS configuration object for Fragment Aggregator class ";
  auto frag_aggr = obj_fac.create("FragmentAggregatorModule", faUid);
  conffwk::ConfigObject fa_net_obj = obj_fac.create_net_obj(fa_net_desc);

  // Process special Network rules!
  // Looking for Fragment rules from DFAppplications in current Session
  std::vector<conffwk::ConfigObject> fragOutObjs;
  for (auto [uid, descriptor]:
         helper->get_netdescriptors("Fragment", "DFApplication")) {
    std::string dreqNetUid(descriptor->get_uid_base() + uid);
    auto frag_conn = obj_fac.create("NetworkConnection", dreqNetUid);

    frag_conn.set_by_val<std::string>("data_type", descriptor->get_data_type());
    frag_conn.set_by_val<std::string>("connection_type", descriptor->get_connection_type());
    // Override capacity, set to 2x expected number of Fragments
    frag_conn.set_by_val<int>("capacity", all_enabled_det_streams.size() * 2);

    auto serviceObj = descriptor->get_associated_service()->config_object();
    frag_conn.set_obj("associated_service", &serviceObj);
    fragOutObjs.push_back(frag_conn);
  }    

  // Add output queueus of data requests and Fragments
  std::vector<const conffwk::ConfigObject*> fa_output_objs;
  for (auto& fNet : fragOutObjs) {
    fa_output_objs.push_back(&fNet);
  }

  for (auto& q : req_queues) {
    fa_output_objs.push_back(&q->config_object());
  }

  frag_aggr.set_obj("configuration", &aggregator_conf->config_object());
  frag_aggr.set_objs("inputs", { &fa_net_obj, &frag_queue_obj });
  frag_aggr.set_objs("outputs", fa_output_objs);

  modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(frag_aggr.UID()));

  obj_fac.update_modules(modules);
} // NOLINT


} // namespace appmodel
} // namespace dunedaq
