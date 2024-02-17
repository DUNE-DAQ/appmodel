/**
 * @file generate_modules.cpp
 *
 * Implementation of TriggerApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ModuleFactory.hpp"

#include "oksdbinterfaces/Configuration.hpp"

#include "coredal/Connection.hpp"
#include "coredal/NetworkConnection.hpp"
#include "coredal/ReadoutGroup.hpp"
#include "coredal/ReadoutInterface.hpp"
#include "coredal/ResourceSet.hpp"
#include "coredal/Service.hpp"
#include "coredal/Session.hpp"

#include "appdal/DataSubscriber.hpp"
#include "appdal/DataReaderConf.hpp"
#include "appdal/DataRecorderConf.hpp"

#include "appdal/ReadoutModule.hpp"
#include "appdal/ReadoutModuleConf.hpp"

#include "appdal/NetworkConnectionRule.hpp"
#include "appdal/QueueConnectionRule.hpp"

#include "appdal/QueueDescriptor.hpp"
#include "appdal/NetworkConnectionDescriptor.hpp"

#include "appdal/SourceIDConf.hpp"

#include "appdal/TriggerApplication.hpp"
#include "appdal/appdalIssues.hpp"

#include "logging/Logging.hpp"

#include <string>
#include <vector>

using namespace dunedaq;
using namespace dunedaq::appdal;

static ModuleFactory::Registrator __reg__("TriggerApplication",
                                          [](const SmartDaqApplication* smartApp,
                                             oksdbinterfaces::Configuration* confdb,
                                             const std::string& dbfile,
                                             const coredal::Session* session) -> ModuleFactory::ReturnType {
                                            auto app = smartApp->cast<TriggerApplication>();
                                            return app->generate_modules(confdb, dbfile, session);
                                          });

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
oksdbinterfaces::ConfigObject
create_network_connection(std::string uid,
                          const NetworkConnectionDescriptor* ntDesc,
                          oksdbinterfaces::Configuration* confdb,
                          const std::string& dbfile)
{
  auto ntServiceObj = ntDesc->get_associated_service()->config_object();
  oksdbinterfaces::ConfigObject ntObj;
  confdb->create(dbfile, "NetworkConnection", uid, ntObj);
  ntObj.set_by_val<std::string>("data_type", ntDesc->get_data_type());
  ntObj.set_by_val<std::string>("connection_type", ntDesc->get_connection_type());
  ntObj.set_obj("associated_service", &ntServiceObj);

  return ntObj;
}

std::vector<const coredal::DaqModule*>
TriggerApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                     const std::string& dbfile,
                                     const coredal::Session* session) const
{
  std::vector<const coredal::DaqModule*> modules;

  auto ti_conf = get_trigger_inputs_handler();
  auto ti_class = ti_conf->get_template_for();

  // Process the queue rules looking for inputs to our trigger handler modules
  const QueueDescriptor* ti_inputq_desc = nullptr;

  for (auto rule : get_queue_rules()) {
    auto destination_class = rule->get_destination_class();
    auto data_type = rule->get_descriptor()->get_data_type();
    if (destination_class == "ReadoutModule" || destination_class == ti_class) {
      ti_inputq_desc = rule->get_descriptor();
    }
  }
  // Process the network rules looking for the Fragment Aggregator and TP handler data reuest inputs
  const NetworkConnectionDescriptor* req_net_desc = nullptr;
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
    else {
	tout_net_desc = rule->get_descriptor();
    }
  }

  // Now create the Data Handler and its associated queue and network
  // connections
  oksdbinterfaces::ConfigObject input_queue_obj;
  oksdbinterfaces::ConfigObject req_net_obj;
  oksdbinterfaces::ConfigObject tout_net_obj;
  oksdbinterfaces::ConfigObject tset_out_net_obj;
  auto handlerConf = get_trigger_inputs_handler();
  
  if ( req_net_obj== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to receive request and send data was set"));
  }
  if ( tout_net_obj== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to publish trigger objects"));
  }
  if ( tset_out_net_obj== nullptr) {
      throw (BadConf(ERS_HERE, "No network descriptor given to publish trigger set objects"));
  }
  if (ti_inputq_desc == nullptr) {
      throw (BadConf(ERS_HERE, "No data input queue descriptor given"));
  }
    
  std::string queue_uid(ti_inputq_desc->get_uid_base());
  confdb->create(dbfile, "Queue", queue_uid, input_queue_obj);
  input_queue_obj.set_by_val<std::string>("data_type", ti_inputq_desc->get_data_type());
  input_queue_obj.set_by_val<std::string>("queue_type", ti_inputq_desc->get_queue_type());
  input_queue_obj.set_by_val<uint32_t>("capacity", ti_inputq_desc->get_capacity());

  auto req_service_obj = req_net_desc->get_associated_service()->config_object();
  std::string req_net_uid(req_net_desc->get_uid_base()+UID());;
  confdb->create(dbfile, "NetworkConnection", req_net_uid, req_net_obj);
  req_net_obj.set_by_val<std::string>("connection_type", req_net_desc->get_connection_type());
  req_net_obj.set_by_val<std::string>("data_type", req_net_desc->get_data_type());
  req_net_obj.set_obj("associated_service", &req_service_obj);

  auto tout_service_obj = tout_net_desc->get_associated_service()->config_object();
  std::string t_stream_uid(tout_net_desc->get_uid_base()+UID());
  confdb->create(dbfile, "NetworkConnection", t_stream_uid, tout_net_obj);
  tout_net_obj.set_by_val<std::string>("data_type", tout_net_desc->get_data_type());
  tout_net_obj.set_by_val<std::string>("connection_type", tout_net_desc->get_connection_type());
  tout_net_obj.set_obj("associated_service", &tout_service_obj);
 


  if (tset_out_net_desc) {
    auto tset_out_service_obj = tset_out_net_desc->get_associated_service()->config_object();
    std::string tset_stream_uid(tset_out_net_desc->get_uid_base()+UID());
    confdb->create(dbfile, "NetworkConnection", tset_stream_uid, tset_out_net_obj);
    tset_out_net_obj.set_by_val<std::string>("data_type", tset_out_net_desc->get_data_type());
    tset_out_net_obj.set_by_val<std::string>("connection_type", tset_out_net_desc->get_connection_type());
    tset_out_net_obj.set_obj("associated_service", &tset_out_service_obj);
  }

  auto ti_conf_obj = ti_conf->config_object();
  oksdbinterfaces::ConfigObject ti_obj;
  std::string ti_uid("tihandler-"+std::to_string(get_source_id()));
  confdb->create(dbfile, ti_class, ti_uid, ti_obj);
  ti_obj.set_by_val<uint32_t>("source_id", get_source_id());
  ti_obj.set_obj("module_configuration", &ti_conf_obj);
  ti_obj.set_objs("inputs", {&input_queue_obj, &req_net_obj});
  if (tset_out_net_desc!= nullptr) {
    ti_obj.set_objs("outputs", {&tout_net_obj, &tset_out_net_obj});
  }
  else {
    ti_obj.set_objs("outputs", {&tout_net_obj});
  }
  // Add to our list of modules to return
   modules.push_back(confdb->get<ReadoutModule>(ti_uid));
  

  // Now create the DataSubscriber object
  auto rdr_conf = get_data_subscriber();
  if (rdr_conf == nullptr) {
    throw (BadConf(ERS_HERE, "No DataReader configuration given"));
  }

  // Create a DataReader 

  std::string reader_uid("data-reader-"+UID());
  std::string reader_class = rdr_conf->get_template_for();
  oksdbinterfaces::ConfigObject reader_obj;
  TLOG_DEBUG(7) <<  "creating OKS configuration object for Data subscriber class " << reader_class;
  confdb->create(dbfile, reader_class, reader_uid, reader_obj);
  reader_obj.set_objs("outputs", {&input_queue_obj} );
  reader_obj.set_obj("configuration", &rdr_conf->config_object());

  modules.push_back(confdb->get<DataSubscriber>(reader_uid));

  return modules;
}
