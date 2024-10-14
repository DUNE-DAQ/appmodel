#ifndef MODULEFACTORY_HPP
#define MODULEFACTORY_HPP

#include "logging/Logging.hpp"
#include "appmodel/appmodelIssues.hpp"

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "confmodel/DaqModule.hpp"
#include "confmodel/System.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "conffwk/Configuration.hpp"

namespace dunedaq::confmodel {
  class DaqModule;
  class System;
}
namespace dunedaq::conffwk {
  class Configuration;
}
namespace dunedaq::appmodel {
  class SmartDaqApplication;

  class ModuleFactory {
  public:
    typedef std::vector<const dunedaq::confmodel::DaqModule*> ReturnType;

    typedef std::function<
      ReturnType(const SmartDaqApplication*,
      dunedaq::conffwk::Configuration*, const std::string&,
      const dunedaq::confmodel::System*)> Generator;

    struct Registrator {
      /**
       * Use this constructor to declare an instance that registers a
       * new factory.
       *
       * @param type      The type name of the new factory.
       * @param generator A function that calls the generate_modules
       *                 method of the class type
       */
      Registrator(const std::string& type, const Generator& generator) :
        m_type(type) {
        ModuleFactory::instance().registerGenerator(type, generator);
      }

      ~Registrator() {
        ModuleFactory::instance().unregisterGenerator(m_type);
      }
    private:
      const std::string m_type;
    }; // Registrator

    static ModuleFactory & instance() {
      static ModuleFactory * factory = new ModuleFactory(); // this will cause memory leak but we don't care
      return *factory;
    }

    ReturnType generate(const std::string& type,
                        const SmartDaqApplication* app,
                        conffwk::Configuration* confdb,
                        const std::string& dbfile,
                        const confmodel::System* system) {
      std::unique_lock lock(m_mutex);
      auto it = m_generators.find(type);
      if (it != m_generators.end()) {
        return it->second(app, confdb, dbfile, system);
      }
      throw BadConf(ERS_HERE, "No '" + type + "' ModuleFactory found");
    }

    void registerGenerator(const std::string& type, const Generator& generator) {
      std::unique_lock lock(m_mutex);
      if (not m_generators.count(type)) {
        m_generators[type] = generator;
        TLOG_DEBUG(11) << "'" << type << "' module factory has been registered";
      } else {
        ers::error(BadConf(ERS_HERE,
                           "The '" + type + "' ModuleFactory is already registered"));
      }
    }

    void unregisterGenerator(const std::string & type) {
      std::unique_lock lock(m_mutex);
      if (m_generators.count(type)) {
        m_generators.erase(type);
        TLOG_DEBUG(11) << "'" << type << "' module factory has been unregistered";
      } else {
        ers::error(BadConf(ERS_HERE,
                           "The '" + type + "' ModuleFactory is unknown"));
      }
    }

  private:
    ModuleFactory() = default;

    std::mutex m_mutex;
    std::map<std::string, Generator> m_generators;


  }; // ModuleFactory

} // namespace dunedaq::appmodel
#endif // MODULEFACTORY
