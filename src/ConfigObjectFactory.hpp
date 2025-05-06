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

#include "conffwk/Configuration.hpp"

#include "confmodel/DetectorStream.hpp"
#include "confmodel/Service.hpp"
#include "oks/file.hpp"

#include <fmt/core.h> // Replace with std::format when we switch to a newer compiler?

namespace dunedaq::appmodel {

class ConfigObjectFactory
{

  conffwk::Configuration* m_config;
  std::string m_dbfile;
  std::string m_app_uid;

public:
  ConfigObjectFactory(conffwk::Configuration* config, const std::string& dbfile, const std::string& app_uid);
  ConfigObjectFactory(const conffwk::DalObject* );

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
};

} // namespace dunedaq::appmodel

#endif // APPMODEL_INCLUDE_OBJECTFACTORY_HPP_
