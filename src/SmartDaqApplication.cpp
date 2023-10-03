#include "ModuleFactory.hpp"
#include "appdal/SmartDaqApplication.hpp"
#include "appdalIssues.hpp"

using namespace dunedaq::appdal;

std::vector<const dunedaq::coredal::DaqModule*>
SmartDaqApplication::generate_modules(oksdbinterfaces::Configuration* confdb,
                                      const std::string& dbfile,
                                      const coredal::Session* session) const {
  return ModuleFactory::instance().generate(class_name(),
                                            this,
                                            confdb,
                                            dbfile,
                                            session);
}
