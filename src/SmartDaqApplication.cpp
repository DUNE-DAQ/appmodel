#include "ModuleFactory.hpp"
#include "readoutdal/SmartDaqApplication.hpp"
#include "readoutdalIssues.hpp"

using namespace dunedaq::readoutdal;

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
