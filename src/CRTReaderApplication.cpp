/**
 * @file CRTReaderApplication.cpp
 *
 * Implementation of CRTReaderApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/CRTReaderApplication.hpp"

#include "appmodel/appmodelIssues.hpp"

#include "appmodel/DataReaderConf.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "appmodel/SocketWriterModule.hpp"
#include "appmodel/DataMoveCallbackConf.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"

#include "ConfigObjectFactory.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"

#include "logging/Logging.hpp"

#include <fmt/core.h>

#include <string>
#include <vector>
#include <memory>

namespace dunedaq::appmodel {

std::vector<const confmodel::Resource*>
CRTReaderApplication::contained_resources() const {
  return to_resources(get_detector_connections());
}

void
  CRTReaderApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{

  TLOG_DEBUG(6) << "Generating modules for application " << this->UID();
  
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);

  //
  // Extract basic configuration objects
  //

  // Data reader  
  const auto reader_conf = get_data_reader();
  if (reader_conf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataReaderModule configuration given"));
  }  
  const std::string reader_class = reader_conf->get_template_for();
  
  // Data writer  
  const auto writer_conf = get_data_writer();
  if (writer_conf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriterModule configuration given"));
  }    
  const std::string writer_class = writer_conf->get_template_for();

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

  // Loop over the detector to daq connections and generate:
  // - One data reader per detector connection
  // - One data writer per detector connection

  uint16_t conn_idx = 0; // NOLINT(build/unsigned)

  for (auto d2d_conn : get_detector_connections()) {

    // Are we sure?
    if (helper->is_disabled(d2d_conn)) {
      TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn->UID();
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn->UID();
    
    std::vector<const confmodel::DetectorStream*> enabled_det_streams;
    // Loop over streams
    for (auto stream : d2d_conn->streams()) {

      // Are we sure?
      if (helper->is_disabled(stream)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorStream " << stream->UID();
        continue;
      }

      enabled_det_streams.push_back(stream);
    }

    // Create the raw data callbacks
    std::vector<const conffwk::ConfigObject*> raw_data_callback_objs;

    // Create data queues
    for (auto ds : enabled_det_streams) {
      conffwk::ConfigObject callback_obj = obj_fac.create_callback_sid_obj(raw_data_callback_desc, ds->get_source_id());
      const auto* callback_conf = obj_fac.get_dal<DataMoveCallbackConf>(callback_obj.UID());
      raw_data_callback_objs.push_back(&callback_conf->config_object());
    }  
        
    //-----------------------------------------------------------------
    //
    // Create DataReaderModule object
    //

    //
    // Instantiate DataReaderModule of type CRTBernReaderModule/CRTGrenobleReaderModule
    //

    // Create the Data reader object

    std::string reader_uid(fmt::format("crtreader-{}-{}", this->UID(), std::to_string(conn_idx++)));
    TLOG_DEBUG(6) << fmt::format("creating OKS configuration object for Data reader class {} with id {}", reader_class, reader_uid);
    auto reader_obj = obj_fac.create(reader_class, reader_uid);

    // Populate configuration and interfaces
    reader_obj.set_obj("configuration", &reader_conf->config_object());
    reader_obj.set_objs("connections", { &d2d_conn->config_object() });
    reader_obj.set_objs("raw_data_callbacks", raw_data_callback_objs);

    modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(reader_obj.UID()));

    //-----------------------------------------------------------------
    //
    // Create DataWriterModule object
    //

    //
    // Instantiate DataWriterModule of type SocketWriterModule
    //

    // Create the SocketWriterModule object

    std::string writer_uid(fmt::format("socketwriter-{}-{}", this->UID(), std::to_string(conn_idx++)));
    TLOG_DEBUG(6) << fmt::format("Creating OKS configuration object for socket writer class {} with id {}", writer_class, writer_uid);
    auto writer_obj = obj_fac.create(writer_class, writer_uid);

    // Populate configuration and interfaces
    writer_obj.set_obj("configuration", &writer_conf->config_object());
    writer_obj.set_objs("connections", {&d2d_conn->config_object()});
    writer_obj.set_objs("raw_data_callbacks", raw_data_callback_objs);

    modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(writer_obj.UID()));    
  }

  obj_fac.update_modules(modules);
}
 
} // namespace dunedaq::appmodel  
