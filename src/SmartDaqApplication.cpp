#include "appdal/SmartDaqApplication.hpp"
#include "appdal/appdalIssues.hpp"

using namespace dunedaq::appdal;

std::vector<const dunedaq::coredal::DaqModule*>
SmartDaqApplication::generate_modules(const std::string& /* dbfile */,
                                      const coredal::Session* /* session */) const {
  throw (BadConf(ERS_HERE,
                 "No generate_modules method defined for smart daq application"));
}
