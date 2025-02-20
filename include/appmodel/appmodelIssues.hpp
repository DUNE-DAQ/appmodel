
#ifndef APPDALISSUES_HPP
#define APPDALISSUES_HPP

#include "ers/Issue.hpp"
#include "logging/Logging.hpp" // NOTE: if ISSUES ARE DECLARED BEFORE include logging/Logging.hpp, TLOG_DEBUG<<issue wont work.

namespace dunedaq {
  ERS_DECLARE_ISSUE(appmodel, BadConf, what, ((std::string)what))
  ERS_DECLARE_ISSUE(appmodel, BadStreamConf,
                    "Failed to cast stream parameters " << id << " to " << stype,
                    ((std::string)id) ((std::string)stype))

  ERS_DECLARE_ISSUE(appmodel,
		    MissingIP,
		    "Daphne configuration has no IP " << ip,
		    ((std::string)ip))

  ERS_DECLARE_ISSUE(appmodel,
		    MissingDaphne,
		    "Daphne " << id << " has active channels but its turned off",
		    ((size_t)id))


  ERS_DECLARE_ISSUE(appmodel,
                    NotSmart,
                    "Object is not a SmartDaqApplication: " << obj,
                    ((std::string)obj))

  ERS_DECLARE_ISSUE(appmodel,
                    BadD2d,
                    "Contained object is not a DetectorToDaqConnection: " << obj,
                    ((std::string)obj))

} // namespace dunedaq


#endif // APPDALISSUES_HPP
