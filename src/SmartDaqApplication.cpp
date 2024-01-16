#include "ModuleFactory.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdal/appdalIssues.hpp"
#include "oks/kernel.hpp"

using namespace dunedaq::appdal;

std::vector<const dunedaq::coredal::DaqModule*>
SmartDaqApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                      const std::string& dbfile,
                                      const coredal::Session* session) const {
  oks::OksFile::set_nolock_mode(true);
  return ModuleFactory::instance().generate(class_name(),
                                            this,
                                            confdb,
                                            dbfile,
                                            session);
}
