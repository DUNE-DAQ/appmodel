/**
 * @file SNBBookkeeperApplication.cpp
 *
 * Implementation of SNBBookkeeperApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ConfigObjectFactory.hpp"
#include "appmodel/SNBBookkeeperApplication.hpp"
#include "appmodel/SNBBookkeeperConf.hpp"
#include "appmodel/SNBTransferBookkeeper.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/QueueConnectionRule.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/appmodelIssues.hpp"
#include "conffwk/Configuration.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Service.hpp"
#include "logging/Logging.hpp"
#include "oks/kernel.hpp"

#include <string>
#include <vector>

namespace dunedaq {
namespace appmodel {

void
SNBBookkeeperApplication::generate_modules(std::shared_ptr<appmodel::ConfigurationHelper> /*helper*/) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  std::string snbBookkeeperUid("snb-sample-config-" + UID());
  TLOG_DEBUG(7) << "creating OKS configuration object for SNBBookkeeperModule class ";
  auto snbBookkeeperObj = obj_fac.create("SNBTransferBookkeeper", snbBookkeeperUid);

  auto snbBookkeeperConf = get_snbbk();
  snbBookkeeperObj.set_obj("configuration", &snbBookkeeperConf->config_object());

  if (snbBookkeeperConf == 0) {
    throw(BadConf(ERS_HERE, "No SNBBookkeeperConf configuration given"));
  }

  std::vector<const conffwk::ConfigObject*> input_conns;
  conffwk::ConfigObject notificationInObj;

  for (auto rule : get_network_rules()) {
    auto endpoint_class = rule->get_endpoint_class();
    auto descriptor = rule->get_descriptor();

    auto connObj = obj_fac.create_net_obj(descriptor, "");

    if (descriptor->get_data_type() == "notification_t") {
      notificationInObj = connObj;
      input_conns.push_back(&notificationInObj);
    }
  }

  if (notificationInObj == nullptr) {
    throw(BadConf(ERS_HERE, "No Notification input connection descriptor given"));
  }

  snbBookkeeperObj.set_objs("inputs", input_conns);

  // Add to our list of modules to return
  modules.push_back(obj_fac.get_dal<SNBTransferBookkeeper>(snbBookkeeperUid));

  obj_fac.update_modules(modules);
}

} // namespace appmodel
} // namespace dunedaq
