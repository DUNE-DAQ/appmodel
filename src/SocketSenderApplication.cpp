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
#include "appmodel/FakeSocketWriterModule.hpp"
#include "appmodel/SocketSenderApplication.hpp"
#include "appmodel/SocketWriterConf.hpp"

#include "confmodel/DetectorToDaqConnection.hpp"

#include "logging/Logging.hpp"
#include <fmt/core.h>

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

//-----------------------------------------------------------------------------

std::vector<const confmodel::Resource*>
SocketSenderApplication::contained_resources() const {
  return to_resources(get_detector_connections());
}

std::vector<const confmodel::DaqModule*>
SocketSenderApplication::generate_modules(const confmodel::Session* session) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  //
  // Extract basic configuration objects
  //
  // Data writers
  const auto writer_confs = get_data_writers();
  for (const auto& writer_conf : writer_confs) {
    if (writer_conf == 0) {
      throw(BadConf(ERS_HERE, "No DataWriterModule configuration given"));
    }

    std::string writer_class = writer_conf->get_template_for();

    std::vector<const conffwk::ConfigObject*> d2d_conn_objs;
    for (auto d2d_conn_res : get_detector_connections()) {
      // Are we sure?
      if (d2d_conn_res->is_disabled(*session)) {
        TLOG_DEBUG(7) << "Ignoring disabled DetectorToDaqConnection " << d2d_conn_res->UID();
        continue;
      }

      d2d_conn_objs.push_back(&d2d_conn_res->config_object());
    }

    //-----------------------------------------------------------------
    //
    // Create DataWriterModule object
    //

    //
    // Instantiate DataWriterModule of type FakeSocketWriterModule
    //

    // Create the FakeSocketWriterModule object
    uint16_t conn_idx = 0;
    std::string writer_uid(fmt::format("socketdatawriter-{}-{}", this->UID(), std::to_string(conn_idx++)));

    TLOG_DEBUG(6) << fmt::format(
      "Creating OKS configuration object for socket data writer class {} with id {}", writer_class, writer_uid);

    auto writer_obj = obj_fac.create(writer_class, writer_uid);

    // Populate configuration and interfaces
    writer_obj.set_obj("configuration", &writer_conf->config_object());
    writer_obj.set_objs("connections", d2d_conn_objs);

    modules.push_back(obj_fac.get_dal<confmodel::DaqModule>(writer_uid));
  }
  return modules;
}
 
} // namespace appmodel  
} // namespace dunedaq
