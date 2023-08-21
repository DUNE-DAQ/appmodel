#include "readoutdal/SmartDaqApplication.hpp"
#include "readoutdalIssues.hpp"

using namespace dunedaq::readoutdal;

std::vector<const dunedaq::coredal::DaqModule*>
SmartDaqApplication::generate_modules(oksdbinterfaces::Configuration*,
                                      const std::string&,
                                      const coredal::Session*) const {
  throw (BadConf(ERS_HERE, "Method not overridden"));
}
