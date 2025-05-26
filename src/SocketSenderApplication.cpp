/**
 * @file SocketSenderApplication.cpp
 *
 * Implementation of SocketSenderApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "ConfigObjectFactory.hpp"

#include "appmodel/appmodelIssues.hpp"
#include "appmodel/SocketWriterModule.hpp"
#include "appmodel/SocketSenderApplication.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"

#include "logging/Logging.hpp"
#include <fmt/core.h>

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

std::vector<const confmodel::DaqModule*>
SocketSenderApplication::generate_modules(conffwk::Configuration* config,
                                          const std::string& dbfile,
                                          const confmodel::Session* session) const
{

  TLOG_DEBUG(6) << "Generating modules for application " << this->UID();
  
  std::vector<const confmodel::DaqModule*> modules;

  const auto obj_fac = ConfigObjectFactory(this);

  //
  // Extract basic configuration objects
  //
  // Data writers
  const auto writer_confs = get_data_writers();
  for (const auto writer_conf : writer_confs) {
    if (writer_conf == 0) { // FIXME (DTE): I don't know about this check
      throw(BadConf(ERS_HERE, "No DataWriterModule configuration given"));
    }

    const std::string writer_class = writer_conf->get_template_for();

    //
    // Process the queue rules looking for inputs to our DL/TP handler modules
    //
    const QueueDescriptor* dlh_input_qdesc = nullptr;

    for (auto rule : get_queue_rules()) {
      auto destination_class = rule->get_destination_class();
      auto data_type = rule->get_descriptor()->get_data_type();
      // Why datahandler here?
      if (destination_class == "FDDataHandlerModule") {
        if (data_type == "DataRequest") {
          //dlh_reqinput_qdesc = rule->get_descriptor();
        } else {
          dlh_input_qdesc = rule->get_descriptor();
        }
      }
    }

    std::map<uint32_t, const confmodel::Connection*> data_queues_by_sid;

    uint16_t conn_idx = 0;

    for (auto d2d_conn_res : get_contains()) {

      // Are we sure?
      if (d2d_conn_res->disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
        continue;
      }

      TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn_res->UID();
      // get the readout groups and the interfaces and streams therein; 1 reaout group corresponds to 1 data reader module      
      auto d2d_conn = d2d_conn_res->cast<confmodel::DetectorToDaqConnection>();

      if (!d2d_conn) {
        throw(BadConf(ERS_HERE, "SocketSenderApplication contains something other than DetectorToDaqConnection"));
      }

      if (d2d_conn->get_contains().empty()) {
        throw(BadConf(ERS_HERE, "DetectorToDaqConnection does not contain senders or receivers"));
      }      

      std::vector<const confmodel::DetectorStream*> enabled_det_streams;      
      // Loop over senders
      for (auto stream : d2d_conn->get_streams()) {

        // Are we sure?
        if (stream->disabled(*session)) {
          TLOG_DEBUG(7) << "Ignoring disabled DetectorStream " << stream->UID();
          continue;
        }

        // loop over streams
        enabled_det_streams.push_back(stream);
      }
      
      //-----------------------------------------------------------------
      //
      // Create DataWriterModule object
      //

      //
      // Instantiate DataWriterModule of type SocketWriterModule
      //

      // Create the SocketWriterModule object
      std::string writer_uid(fmt::format("socketdatawriter-{}-{}", this->UID(), std::to_string(conn_idx++)));
      TLOG_DEBUG(6) << fmt::format(
        "Creating OKS configuration object for socket data writer class {} with id {}", writer_class, writer_uid);
      auto writer_obj = obj_fac.create(writer_class, writer_uid);

      // Populate configuration and interfaces
      writer_obj.set_obj("configuration", &writer_conf->config_object());
      writer_obj.set_objs("connections", {&d2d_conn_res->config_object()});

      // Create the raw data queues
      std::vector<const conffwk::ConfigObject*> data_queue_objs;
      // keep a map for convenience

      // Create data queues
      for (auto ds : enabled_det_streams) {
        conffwk::ConfigObject queue_obj = obj_fac.create_queue_sid_obj(dlh_input_qdesc, ds);
        const auto* data_queue = config->get<confmodel::Connection>(queue_obj.UID());
        data_queue_objs.push_back(&data_queue->config_object());
        data_queues_by_sid[ds->get_source_id()] = data_queue;
      }

      writer_obj.set_objs("outputs", data_queue_objs);

      modules.push_back(config->get<confmodel::DaqModule>(writer_obj.UID()));      
    }
  }
  
  return modules;
}
 
} // namespace appmodel  
} // namespace dunedaq
