/**
 * @file CRTFrameBuilderApplication.cpp
 *
 * Implementation of CRTFrameBuilderApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/CRTFrameBuilderApplication.hpp"

#include "appmodel/appmodelIssues.hpp"

#include "appmodel/DetectorFrameBuilderConf.hpp"
#include "appmodel/SocketWriterConf.hpp"
#include "appmodel/SocketWriterModule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/SocketDetectorToDaqConnection.hpp"

#include "ConfigObjectFactory.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/DetDataSender.hpp"
#include "confmodel/DetDataReceiver.hpp"

#include "logging/Logging.hpp"

#include <fmt/core.h>

#include <string>
#include <vector>
#include <memory>

namespace dunedaq::appmodel {

std::vector<const confmodel::ExcludableEntity*>
CRTFrameBuilderApplication::contained_excludable_entities() const {
  return to_resources(get_detector_connections());
}

void
  CRTFrameBuilderApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> helper) const
{

  TLOG_DEBUG(6) << "Generating modules for application " << this->UID();
  
  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);

  //
  // Extract basic configuration objects
  //

  // Detector frame builder
  const auto det_frame_builder_conf = get_detector_frame_builder();
  if (det_frame_builder_conf == nullptr) {
    throw(BadConf(ERS_HERE, "No DetectorFrameBuilderModule configuration given"));
  }  
  const std::string builder_class = det_frame_builder_conf->get_template_for();
  
  // Data writer  
  const auto writer_conf = get_data_writer();
  if (writer_conf == nullptr) {
    throw(BadConf(ERS_HERE, "No DataWriterModule configuration given"));
  }    
  const std::string writer_class = writer_conf->get_template_for();

  //
  // Process the queue rules looking for inputs to our socket writer modules
  //
  const QueueDescriptor* crtframebuilder_output_qdesc = nullptr;
  auto queue_rules = get_queue_rules();
  if (queue_rules.size() != 1) {
    throw(BadConf(ERS_HERE, "Strictly 1 queue rule is expected"));
  }
  crtframebuilder_output_qdesc = queue_rules[0]->get_descriptor();

  //
  // Scan Detector 2 DAQ connections to extract sender, receiver and stream information
  //

  // Loop over the detector to daq connections and generate:
  // - One detector frame builder per data sender
  // - One data writer per data sender
  // - One queue per data sender

  for (auto d2d_conn : get_detector_connections()) {

    auto d2d_conn_uid = d2d_conn->UID();

    // Are we sure?
    if (helper->is_excluded(d2d_conn)) {
      TLOG_DEBUG(7) << "Ignoring excluded DetectorToDaqConnection " << d2d_conn_uid;
      continue;
    }

    TLOG_DEBUG(6) << "Processing DetectorToDaqConnection " << d2d_conn_uid;

    auto receiver = d2d_conn->receiver();

    uint16_t sender_idx = 0; // NOLINT(build/unsigned)

    // Loop over senders
    for (auto sender : d2d_conn->senders()) {
      
      // Are we sure?
      if (helper->is_excluded(sender)) {
        TLOG_DEBUG(7) << "Ignoring excluded DataSender " << sender->UID();
        continue;
      }

      
      bool has_included_det_stream = false;
      // Loop over streams
      for (auto stream : sender->get_streams()) {
        
        // Are we sure?
        if (helper->is_excluded(stream)) {
          TLOG_DEBUG(7) << "Ignoring excluded DetectorStream " << stream->UID();
          continue;
        }

        has_included_det_stream = true;
        break;
      }
      
      if (!has_included_det_stream) {
        continue;
      }

      const auto sender_idx_str = std::to_string(sender_idx);
      
      // Create a connection that is dedicated to this sender
      std::string sender_conn_uid(d2d_conn_uid + sender_idx_str);
      auto sender_conn_obj = obj_fac.create("SocketDetectorToDaqConnection", sender_conn_uid);
      sender_conn_obj.set_objs("net_senders", { &sender->config_object() });
      sender_conn_obj.set_obj("net_receiver", &receiver->config_object());
      const auto* sender_conn = obj_fac.get_dal<appmodel::SocketDetectorToDaqConnection>(sender_conn_obj.UID());
      const auto* sender_conn_conf_obj = &sender_conn->config_object();

      // Create data queue
      conffwk::ConfigObject queue_obj = obj_fac.create_queue_obj(crtframebuilder_output_qdesc, sender_idx_str);
      const auto* queue = obj_fac.get_dal<confmodel::Connection>(queue_obj.UID());
      const auto* queue_conf_obj = &queue->config_object();

      //-----------------------------------------------------------------
      //
      // Create DetectorFrameBuilderModule object
      //
  
      //
      // Instantiate DetectorFrameBuilderModule of type CRTBernFrameBuilderModule/CRTGrenobleFrameBuilderModule
      //
  
      // Create the detector frame builder object
  
      std::string builder_uid(fmt::format("crt-frame-builder-{}-{}", this->UID(), sender_idx_str));
      TLOG_DEBUG(6) << fmt::format("creating OKS configuration object for detector frame builder class {} with id {}", builder_class, builder_uid);
      auto builder_obj = obj_fac.create(builder_class, builder_uid);
  
      // Populate configuration and interfaces
      builder_obj.set_obj("configuration", &det_frame_builder_conf->config_object());
      builder_obj.set_obj("connection", sender_conn_conf_obj);
      builder_obj.set_objs("outputs", { queue_conf_obj });
  
      modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(builder_obj.UID()));

      //-----------------------------------------------------------------
      //
      // Create DataWriterModule object
      //
  
      //
      // Instantiate DataWriterModule of type SocketWriterModule
      //
  
      // Create the SocketWriterModule object
  
      std::string writer_uid(fmt::format("socket-writer-{}-{}", this->UID(), sender_idx_str));
      TLOG_DEBUG(6) << fmt::format("Creating OKS configuration object for socket writer class {} with id {}", writer_class, writer_uid);
      auto writer_obj = obj_fac.create(writer_class, writer_uid);
  
      // Populate configuration and interfaces
      writer_obj.set_obj("configuration", &writer_conf->config_object());
      writer_obj.set_obj("connection", sender_conn_conf_obj);
      writer_obj.set_objs("inputs", { queue_conf_obj });
  
      modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(writer_obj.UID()));    

      ++sender_idx;
    }
  }

  obj_fac.update_modules(modules);
}
 
} // namespace dunedaq::appmodel  
