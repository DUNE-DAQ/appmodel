/**
 * @file ConfigObjectFactory.hpp
 *
 * Define a helper class for SmartDaqApplication module generators
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */
#ifndef APPMODEL_INCLUDE_OBJECTFACTORY_HPP_
#define APPMODEL_INCLUDE_OBJECTFACTORY_HPP_

#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/QueueDescriptor.hpp"
#include "appmodel/SmartDaqApplication.hpp"

#include "conffwk/ConfigObject.hpp"
#include "conffwk/Configuration.hpp"

#include "confmodel/DaqModule.hpp"
#include "confmodel/DetectorStream.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dunedaq::appmodel {

class ConfigObjectFactory
{

  conffwk::Configuration* m_config;
  std::string m_dbfile;
  std::string m_app_uid;

public:
  ConfigObjectFactory(conffwk::Configuration* config, const std::string& dbfile, const std::string& app_uid);
  explicit ConfigObjectFactory(const conffwk::DalObject* );

  ~ConfigObjectFactory();

  [[nodiscard]] conffwk::ConfigObject
  create(const std::string& class_name, const std::string& id) const;

  //---
  [[nodiscard]] conffwk::ConfigObject
  create_queue_obj(const QueueDescriptor* qdesc, std::string uid = "") const;

  //---
  [[nodiscard]] conffwk::ConfigObject
  create_queue_sid_obj(const QueueDescriptor* qdesc, uint32_t src_id) const;

  //---
  [[nodiscard]] conffwk::ConfigObject
  create_queue_sid_obj(const QueueDescriptor* qdesc, const confmodel::DetectorStream* stream) const;
  //---

  /**
   * \brief Helper function that gets a network connection config
   *
   * \param uid  Unique ID name of the config object
   * \param ndesc Network connection descriptor object
   *
   * \ret OKS configuration object for the network connection
   */
  [[nodiscard]] conffwk::ConfigObject
  create_net_obj(const NetworkConnectionDescriptor* ndesc, std::string uid) const;

  [[nodiscard]] conffwk::ConfigObject
  create_net_obj(const NetworkConnectionDescriptor* ndesc) const;

  template<class T>
  const T* get_dal(std::string uid) const {
    return m_config->get<T>(uid);
  }

  template<class T>
  const T* get_dal(conffwk::ConfigObject& obj) const {
    return m_config->get<T>(obj);
  }

  void
  update_modules(const std::vector<const confmodel::DaqModule*>& modules) {
    auto app = m_config->get<SmartDaqApplication>(m_app_uid);
    const_cast<SmartDaqApplication*>(app)->set_modules(modules);
    m_config->update<SmartDaqApplication>({m_app_uid}, {}, {});
  }

};

} // namespace dunedaq::appmodel

#endif // APPMODEL_INCLUDE_OBJECTFACTORY_HPP_
