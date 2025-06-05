/**
 * @file TriggerApplication.cpp
 *
 * Implementation of TriggerApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */


#include "conffwk/Configuration.hpp"

#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/ResourceSet.hpp"
#include "confmodel/Service.hpp"
#include "confmodel/Session.hpp"

#include "ConfigObjectFactory.hpp"
#include "appmodel/DataSubscriberModule.hpp"
#include "appmodel/DataReaderConf.hpp"
#include "appmodel/DataRecorderConf.hpp"

#include "appmodel/DataHandlerModule.hpp"
#include "appmodel/DataHandlerConf.hpp"

#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"

#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"

#include "appmodel/SourceIDConf.hpp"

#include "appmodel/TriggerApplication.hpp"
#include "appmodel/DFApplication.hpp"
#include "appmodel/appmodelIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

/**
 * \brief Helper function that gets a network connection config
 *
 * \param idname Unique ID name of the config object
 * \param ntDesc Network connection descriptor object
 * \param confdb Global database configuration
 * \param dbfile Database file location
 *
 * \ret OKS configuration object for the network connection
 */
conffwk::ConfigObject
create_network_connection(std::string uid,
                          const NetworkConnectionDescriptor* ntDesc,
                          conffwk::Configuration* confdb,
                          const std::string& dbfile)
{
  auto ntServiceObj = ntDesc->get_associated_service()->config_object();
  conffwk::ConfigObject ntObj;
  confdb->create(dbfile, "NetworkConnection", uid, ntObj);
  ntObj.set_by_val<std::string>("data_type", ntDesc->get_data_type());
  ntObj.set_by_val<std::string>("connection_type", ntDesc->get_connection_type());
  ntObj.set_obj("associated_service", &ntServiceObj);

  return ntObj;
}


std::vector<const confmodel::DaqModule*>
TriggerApplication::generate_modules(const confmodel::Session* session) const
{

  std::vector<const confmodel::DaqModule*> modules;

  ConfigObjectFactory obj_fac(this);

  auto ti_conf = get_trigger_inputs_handler();
  auto ti_class = ti_conf->get_template_for();
  std::string handler_name("");
  // Process the queue rules looking for inputs to our trigger handler modules
  const QueueDescriptor* ti_inputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "DataHandlerModule" || destination_class == ti_class) {
      ti_inputq_desc = rule->get_descriptor();
    }
  }
  // Process the network rules looking for the TP handler data reuest inputs
  const NetworkConnectionDescriptor* req_net_desc = nullptr;
  const NetworkConnectionDescriptor* tin_net_desc = nullptr;
  const NetworkConnectionDescriptor* tout_net_desc = nullptr;
  const NetworkConnectionDescriptor* tset_out_net_desc = nullptr;
  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto data_type = rule->get_descriptor()->get_data_type();

    if (data_type == "DataRequest") {
      req_net_desc = rule->get_descriptor();
    }
    else if (data_type == "TASet" || data_type == "TCSet"){
      tset_out_net_desc = rule->get_descriptor();
    }
    else if (endpoint_class == "DataSubscriberModule") {
      if (!tin_net_desc) {
        tin_net_desc =  rule->get_descriptor();
      }
      else if (rule->get_descriptor()->get_data_type() == tin_net_desc->get_data_type()) {
        // For now endpoint_class of DataSubscriberModule for both input and output
        // with the same data type is not possible.
        throw (BadConf(ERS_HERE, "Have two network connections of the same data_type and the same endpoint_class"));
      }
      else if (tin_net_desc->get_data_type() == "TriggerActivity" &&
          rule->get_descriptor()->get_data_type() == "TriggerCandidate") {
        // For TA->TC
        tout_net_desc = rule->get_descriptor();
        handler_name = "tahandler";
      }
      else if (tin_net_desc->get_data_type() == "TriggerCandidate" &&
          rule->get_descriptor()->get_data_type() == "TriggerActivity") {
        // For TA->TC if we saved TC network connection as input first...
        tout_net_desc = tin_net_desc;
        tin_net_desc = rule->get_descriptor();
        handler_name = "tahandler";
      }
      else {
        throw (BadConf(ERS_HERE, "Unexpected input & output network connection descriptors provided"));
      }
    }
    else if (data_type == "TriggerActivity" || data_type == "TriggerCandidate"){
      tout_net_desc = rule->get_descriptor();
      if (data_type == "TriggerActivity")
        handler_name = "tphandler";
      else
        handler_name = "tahandler";
    }
  }

  // Process special Network rules!
  // Looking for Fragment rules from DFAppplications in current Session
  auto sessionApps = session->get_enabled_applications();
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
        // std::string dreqNetUid(descriptor->get_uid_base() + )
        auto frag_conn = obj_fac.create_net_obj(descriptor, dfapp->UID());

        fragOutObjs.push_back(frag_conn);
      } // If network rule has TriggerDecision type of data
    }   // Loop over Apps network rules
  }     // loop over Session specific Apps


  if ( req_net_desc== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to receive request and send data was set"));
  }
  if ( tin_net_desc== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to receive trigger objects"));
  }
  if ( tout_net_desc== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to publish trigger objects"));
  }
  if (ti_inputq_desc == nullptr) {
      throw (BadConf(ERS_HERE, "No data input queue descriptor given"));
  }

  auto input_queue_obj = obj_fac.create_queue_obj(ti_inputq_desc);

  auto req_net_obj = obj_fac.create_net_obj(req_net_desc, UID());

  auto tin_net_obj = obj_fac.create_net_obj(tin_net_desc, ".*");

  auto tout_net_obj = obj_fac.create_net_obj(tout_net_desc, UID());
  conffwk::ConfigObject tset_out_net_obj;
  if (tset_out_net_desc) {
    tset_out_net_obj = obj_fac.create_net_obj(tset_out_net_desc, UID());
  }


  // build up the full list of outputs
  std::vector<const conffwk::ConfigObject*> ti_output_objs;
  for (auto& fNet : fragOutObjs) {
    ti_output_objs.push_back(&fNet);
  }
  ti_output_objs.push_back(&tout_net_obj);
  if (tset_out_net_desc!= nullptr) {
    ti_output_objs.push_back(&tset_out_net_obj);
  }

  if (get_source_id() == nullptr) {
    throw(BadConf(ERS_HERE, "No source_id associated with this TriggerApplication!"));
  }
  uint32_t source_id = get_source_id()->get_sid();
  std::string ti_uid(handler_name + "-" + std::to_string(source_id));
  auto ti_obj = obj_fac.create(ti_class, ti_uid);

  ti_obj.set_by_val<uint32_t>("source_id", source_id);
  ti_obj.set_by_val<uint32_t>("detector_id", 1); // 1 == kDAQ
  ti_obj.set_by_val<bool>("post_processing_enabled", !get_tx_generation_disabled());

  auto ti_conf_obj = ti_conf->config_object();
  ti_obj.set_obj("module_configuration", &ti_conf_obj);
  ti_obj.set_objs("inputs", {&input_queue_obj, &req_net_obj});
  ti_obj.set_objs("outputs", ti_output_objs);
  // Add to our list of modules to return
  modules.push_back(obj_fac.get_dal<DataHandlerModule>(ti_uid));


  // Now create the DataSubscriberModule object
  auto rdr_conf = get_data_subscriber();
  if (rdr_conf == nullptr) {
    throw (BadConf(ERS_HERE, "No DataReaderModule configuration given"));
  }

  // Create a DataReaderModule

  std::string reader_uid("data-reader-"+UID());
  std::string reader_class = rdr_conf->get_template_for();
  TLOG_DEBUG(7) <<  "creating OKS configuration object for Data subscriber class " << reader_class;
  auto reader_obj = obj_fac.create(reader_class, reader_uid);
  reader_obj.set_objs("inputs", {&tin_net_obj} );
  reader_obj.set_objs("outputs", {&input_queue_obj} );
  reader_obj.set_obj("configuration", &rdr_conf->config_object());

  modules.push_back(obj_fac.get_dal<DataSubscriberModule>(reader_uid));

  return modules;
}
 
} // namespace appmodel  
} // namespace dunedaq
