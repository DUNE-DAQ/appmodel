
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
        MissingDaphne,
        "Daphne configuration has no board " << id,
        ((std::string)id))

  ERS_DECLARE_ISSUE(appmodel,
        UnimplementedMethodCalled,
        "Method '" << method_name << "' was called but is not implemented in this class",
        ((std::string)method_name))

}


#endif // APPDALISSUES_HPP
