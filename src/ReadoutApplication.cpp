/**
 * @file generate_modules.cpp
 *
 * Implementation of ReadoutApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "appmodel/ReadoutApplication.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/DetDataReceiver.hpp"
#include "confmodel/DetDataSender.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/Session.hpp"

#include "appmodel/NWDetDataReceiver.hpp"
#include "appmodel/NWDetDataSender.hpp"

#include "appmodel/DPDKReceiver.hpp"
#include "confmodel/QueueWithSourceId.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/GeoId.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"

#include "appmodel/DataReaderModule.hpp"
#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataRecorderModule.hpp"
#include "appmodel/DataRecorderConf.hpp"

#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataHandlerConf.hpp"
#include "appmodel/FragmentAggregatorModule.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/RequestHandler.hpp"

#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"
#include <fmt/core.h>

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appmodel;

static ModuleFactory::Registrator __reg__("ReadoutApplication", [](const SmartDaqApplication* smartApp, conffwk::Configuration* config, const std::string& dbfile, const confmodel::Session* session) -> ModuleFactory::ReturnType {
  auto app = smartApp->cast<ReadoutApplication>();
  return app->generate_modules(config, dbfile, session);
});

class ReadoutObjFactory {
  public:

  conffwk::Configuration* config;
  std::string dbfile;
  std::string app_uid;

  //---
  conffwk::ConfigObject create_queue_obj(const QueueDescriptor* qdesc) {
    conffwk::ConfigObject queue_obj;

    std::string queue_uid(qdesc->get_uid_base());
    config->create(this->dbfile, "Queue", queue_uid, queue_obj);
    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());

    return queue_obj;
  }

  //---
  conffwk::ConfigObject create_queue_sid_obj(const QueueDescriptor* qdesc, uint32_t src_id) {
    conffwk::ConfigObject queue_obj;

    std::string queue_uid(fmt::format("{}{}", qdesc->get_uid_base(), src_id));
    config->create(this->dbfile, "QueueWithSourceId", queue_uid, queue_obj);
    queue_obj.set_by_val<std::string>("data_type", qdesc->get_data_type());
    queue_obj.set_by_val<std::string>("queue_type", qdesc->get_queue_type());
    queue_obj.set_by_val<uint32_t>("capacity", qdesc->get_capacity());
    queue_obj.set_by_val<uint32_t>("source_id", src_id);

    return queue_obj;
  }

  //---
  conffwk::ConfigObject create_queue_sid_obj(const QueueDescriptor* qdesc, const confmodel::DetectorStream* stream) {
    return this->create_queue_sid_obj(qdesc, stream->get_source_id());
  }



  //---
  conffwk::ConfigObject create_net_obj(const NetworkConnectionDescriptor* ndesc, std::string uid) {
    conffwk::ConfigObject net_obj;

    auto svc_obj = ndesc->get_associated_service()->config_object();
    std::string net_id = ndesc->get_uid_base() + uid;
    config->create(this->dbfile, "NetworkConnection", net_id, net_obj);
    net_obj.set_by_val<std::string>("data_type", ndesc->get_data_type());
    net_obj.set_by_val<std::string>("connection_type", ndesc->get_connection_type());
    net_obj.set_obj("associated_service", &svc_obj);

    return net_obj;

  }

  conffwk::ConfigObject create_net_obj(const NetworkConnectionDescriptor* ndesc) {
    return this->create_net_obj(ndesc, this->app_uid);
  }


};

//-----------------------------------------------------------------------------
std::vector<const confmodel::DaqModule*>
ReadoutApplication::generate_modules(conffwk::Configuration* config, const std::string& dbfile, const confmodel::Session* session) const
{

  TLOG() << "Generating modules for application " << this->UID();

  ReadoutObjFactory obj_fac{config, dbfile, this->UID()};
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
  std::string tph_class = "";
  if (tph_conf != nullptr) {
    tph_class = tph_conf->get_template_for();
  }

  //
  // Process the queue rules looking for inputs to our DL/TP handler modules
  //
  const QueueDescriptor* dlh_input_qdesc = nullptr;
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
      } else if (data_type == "TriggerPrimitive") {
        tp_input_qdesc = rule->get_descriptor();
      } else {
        dlh_input_qdesc = rule->get_descriptor();
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
  // Scan Detector 2 DAQ connections to extract sender, receiver and stream information
  //

  std::vector<const confmodel::DaqModule*> modules;

  // Loop over the detector to daq connections and generate one data reader per connection
  // and the cooresponding datalink handlers

  // Collect all streams
  std::vector<const confmodel::DetectorStream*> det_streams;
  std::vector<const conffwk::ConfigObject*> d2d_conn_objs;

  uint16_t conn_idx = 0;
  for (auto d2d_conn_res : get_contains()) {

    // Are we sure?
    if (d2d_conn_res->disabled(*session)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
      continue;
    }

    d2d_conn_objs.push_back(&d2d_conn_res->config_object());

    TLOG() << "Processing DetectorToDaqConnection " << d2d_conn_res->UID();
    // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module
    auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

    if (!d2d_conn) {
      throw(BadConf(ERS_HERE, "ReadoutApplication contains something other than DetectorToDaqConnection"));
    }

    if (d2d_conn->get_contains().empty()) {
      throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain sebders or receivers"));
    }

    // Loop over detector 2 daq connections to find senders and receivers
    auto det_senders = d2d_conn->get_senders();
    auto det_receiver = d2d_conn->get_receiver();

    // Loop over senders
    for (auto stream : d2d_conn->get_streams()) {

      // Are we sure?
      if (stream->disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorStream " << stream->UID();
        continue;
      }

      // loop over streams
      det_streams.push_back(stream);
    }



    // Here I want to resolve the type of connection (network, felix, or?)
    // Rules of engagement: if the receiver interface is network or felix, the receivers should be castable to the counterpart
    if (reader_class == "DPDKReaderModule") {
      if (!det_receiver->cast<appmodel::DPDKReceiver>()) {
        throw(BadConf(ERS_HERE, fmt::format("DPDKReaderModule requires NWDetDataReceiver, found {} of class {}", det_receiver->UID(), det_receiver->class_name())));
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
  }

  //-----------------------------------------------------------------
  //
  // Create DataReaderModule object
  //

  //
  // Instantiate DataReaderModule of type DPDKReaderModule
  //

  // Create the DPDKReaderModule object
  std::string reader_uid(fmt::format("datareader-{}-{}", this->UID(), std::to_string(conn_idx++)));
  conffwk::ConfigObject reader_obj;
  TLOG() << fmt::format("creating OKS configuration object for Data reader class {} with id {}", reader_class, reader_uid);
  config->create(dbfile, reader_class, reader_uid, reader_obj);

  // Populate configuration and interfaces (leave output queues for later)
  reader_obj.set_obj("configuration", &reader_conf->config_object());
  reader_obj.set_objs("connections", d2d_conn_objs);

  // Create the raw data queues
  std::vector<const conffwk::ConfigObject*> data_queue_objs;
  // keep a map for convenience
  std::map<uint32_t, const confmodel::Connection*> data_queues_by_sid;

  // Create data queues
  for (auto ds : det_streams) {
    conffwk::ConfigObject queue_obj = obj_fac.create_queue_sid_obj(dlh_input_qdesc, ds);
    const auto* connection = config->get<confmodel::Connection>(queue_obj.UID());
    data_queue_objs.push_back(&connection->config_object());
    data_queues_by_sid[ds->get_source_id()] = connection;
  }

  reader_obj.set_objs("outputs", data_queue_objs);

  modules.push_back(config->get<confmodel::DaqModule>(reader_uid));

  //-----------------------------------------------------------------
  //
  // Prepare the tp handler and related queues
  //
  conffwk::ConfigObject tp_queue_obj;
  conffwk::ConfigObject tpreq_queue_obj;

  if (tph_conf) {

    auto tpsrc = get_tp_source_id();

    // Create TP handler object
    auto tph_conf_obj = tph_conf->config_object();
    conffwk::ConfigObject tph_obj;
    std::string tp_uid("tphandler-" + std::to_string(tpsrc));
    config->create(dbfile, tph_class, tp_uid, tph_obj);
    tph_obj.set_by_val<uint32_t>("source_id", tpsrc);
    tph_obj.set_obj("module_configuration", &tph_conf_obj);

    // Create the TPs aggregator queue (from RawData Handlers to TP handlers)
    tp_queue_obj = obj_fac.create_queue_obj(tp_input_qdesc);
    tp_queue_obj.set_by_val<uint32_t>("recv_timeout_ms", 1);
    tp_queue_obj.set_by_val<uint32_t>("send_timeout_ms", 1);

    // Create tp data requests queue from Fragment Aggregator
    tpreq_queue_obj = obj_fac.create_queue_sid_obj(dlh_reqinput_qdesc, tpsrc);
    req_queues.push_back(config->get<confmodel::Connection>(tpreq_queue_obj.UID()));

    // Create the tp(set) publishing service
    conffwk::ConfigObject tp_net_obj = obj_fac.create_net_obj(tp_net_desc);

    // Create the ta(set) publishing service
    conffwk::ConfigObject ta_net_obj = obj_fac.create_net_obj(ta_net_desc);

    // Register queues with tp hankder
    tph_obj.set_objs("inputs", { &tp_queue_obj, &tpreq_queue_obj });
    tph_obj.set_objs("outputs", { &tp_net_obj, &ta_net_obj, &frag_queue_obj });
    modules.push_back(config->get<confmodel::DaqModule>(tp_uid));
  }

  //-----------------------------------------------------------------
  //
  // Create datalink handlers
  //
  // Recover the emulation flag
  auto emulation_mode = reader_conf->get_emulation_mode();
  for (auto ds : det_streams) {

    uint32_t sid = ds->get_source_id();
    TLOG() << fmt::format("Processing stream {}, id {}, det id {}", ds->UID(), ds->get_source_id(), ds->get_geo_id()->get_detector_id());
    std::string uid(fmt::format("DLH-{}", sid));
    conffwk::ConfigObject dlh_obj;
    TLOG() << fmt::format("creating OKS configuration object for Data Link Handler class {}, if {}", dlh_class, sid);
    config->create(dbfile, dlh_class, uid, dlh_obj);
    dlh_obj.set_by_val<uint32_t>("source_id", sid);
    dlh_obj.set_by_val<bool>("emulation_mode", emulation_mode);
    dlh_obj.set_obj("geo_id", &ds->get_geo_id()->config_object());
    dlh_obj.set_obj("module_configuration", &dlh_conf->config_object());

    std::vector<const conffwk::ConfigObject*> dlh_ins, dlh_outs;

    // Add datalink-handler queue to the inputs
    dlh_ins.push_back(&data_queues_by_sid.at(sid)->config_object());

    // Create request queue
    conffwk::ConfigObject req_queue_obj = obj_fac.create_queue_sid_obj(dlh_reqinput_qdesc, ds);


    // Add the requessts queue dal pointer to the outputs of the FragmentAggregatorModule
    req_queues.push_back(config->get<confmodel::Connection>(req_queue_obj.UID()));
    dlh_ins.push_back(&req_queue_obj);
    dlh_outs.push_back(&frag_queue_obj);


    // Time Sync network connection
    if (dlh_conf->get_generate_timesync()) {
      // Add timestamp endpoint
      conffwk::ConfigObject ts_net_obj = obj_fac.create_net_obj(ts_net_desc, std::to_string(sid));

      dlh_outs.push_back(&ts_net_obj);
    }

    if (tph_conf) {
      dlh_outs.push_back(&tp_queue_obj);
    }

    dlh_obj.set_objs("inputs", dlh_ins);
    dlh_obj.set_objs("outputs", dlh_outs);

    modules.push_back(config->get<confmodel::DaqModule>(uid));
  }


  // Finally create Fragment Aggregator
  std::string faUid("fragmentaggregator-" + UID());
  conffwk::ConfigObject frag_aggr;
  TLOG_DEBUG(7) << "creating OKS configuration object for Fragment Aggregator class ";
  config->create(dbfile, "FragmentAggregatorModule", faUid, frag_aggr);

  // Add network connection to TRBs
  conffwk::ConfigObject fa_net_obj = obj_fac.create_net_obj(fa_net_desc);

  // Add output queueus of data requests
  std::vector<const conffwk::ConfigObject*> req_queue_objs;
  for (auto q : req_queues) {
    req_queue_objs.push_back(&q->config_object());
  }

  frag_aggr.set_objs("inputs", { &fa_net_obj, &frag_queue_obj });
  frag_aggr.set_objs("outputs", req_queue_objs);



  return modules;
}
