/**
 * @file SNBTransferApplication.cpp
 *
 * Implementation of SNBTransferApplication's generate_modules dal method
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "ConfigObjectFactory.hpp"
#include "appmodel/SNBTransferApplication.hpp"
#include "appmodel/SNBTransferConf.hpp"
#include "appmodel/SNBFileTransfer.hpp"
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

std::vector<const confmodel::DaqModule*>
SNBTransferApplication::generate_modules(const confmodel::Session* /*session*/) const
{
  ConfigObjectFactory obj_fac(this);

  std::vector<const confmodel::DaqModule*> modules;

  std::string snbTransferUid("snb-sample-config-" + UID());
  TLOG_DEBUG(7) << "creating OKS configuration object for SNBTransferModule class ";
  auto snbTransferObj = obj_fac.create("SNBFileTransfer", snbTransferUid);

  auto snbTransferConf = get_snbt();
  snbTransferObj.set_obj("configuration", &snbTransferConf->config_object());

  if (snbTransferConf == 0) {
    throw(BadConf(ERS_HERE, "No SNBTransferConf configuration given"));
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

  snbTransferObj.set_objs("inputs", input_conns);

  // Add to our list of modules to return
  modules.push_back(obj_fac.get_dal<SNBFileTransfer>(snbTransferUid));

  return modules;
}

} // namespace appmodel
} // namespace dunedaq
