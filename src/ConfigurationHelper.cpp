/**
 * @file generate_modules.cpp
 *
 * Implementation of ConfigurationHelper class
 *
 * This is part of the DUNE DAQ Software Suite, copyright 2023.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#include "appmodel/appmodelIssues.hpp"
#include "appmodel/ConfigurationHelper.hpp"
#include "appmodel/FakeDataApplication.hpp"
#include "appmodel/FakeDataProdConf.hpp"
#include "appmodel/NetworkConnectionDescriptor.hpp"
#include "appmodel/NetworkConnectionRule.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/SourceIDConf.hpp"
#include "conffwk/ConfigObject.hpp"
#include "conffwk/Schema.hpp"
#include "confmodel/DetectorStream.hpp"
#include "confmodel/DetectorToDaqConnection.hpp"
#include "confmodel/NetworkConnection.hpp"
#include "confmodel/Queue.hpp"
#include "confmodel/Resource.hpp"
#include "confmodel/Service.hpp"
#include "confmodel/Session.hpp"

using namespace dunedaq;
using namespace dunedaq::appmodel;

std::vector<std::pair<std::string, const appmodel::NetworkConnectionDescriptor*>>
ConfigurationHelper::get_netdescriptors(
  const std::string& data_type,
  const std::string& app_class) {
    std::vector<std::pair<std::string, const appmodel::NetworkConnectionDescriptor*>>
      result;
    for (auto app: m_session->enabled_applications()) {
      if (app_class.empty() || app->castable(app_class)) {
        auto smart_app = app->cast<appmodel::SmartDaqApplication>();
        if (smart_app == nullptr) {
          // Only SmartDaqApplications have network rules
          continue;
        }
        for (auto rule: smart_app->get_network_rules()) {
          auto desc = rule->get_descriptor();
          if (desc->get_data_type() == data_type) {
            result.emplace_back(std::pair{app->UID(), desc});
          }
        }
      }
    }
    return result;
}


std::vector<const confmodel::Service*> ConfigurationHelper::get_services(
  std::string app_class,
  std::string data_type)
{
  std::vector<const confmodel::Service*> result;
  for (auto app: m_session->enabled_applications()) {
    if (app->castable(app_class)) {
      auto smart_app = app->cast<appmodel::SmartDaqApplication>();
      if (smart_app == nullptr) {
        throw (NotSmart(ERS_HERE, app->full_name()));
      }
      for (auto rule: smart_app->get_network_rules()) {
        if (rule->get_descriptor()->get_data_type() == data_type) {
          result.push_back(rule->get_descriptor()->get_associated_service());
        }
      }
    }
  }
  return result;
}


std::map<std::string,std::vector<uint32_t>> ConfigurationHelper::get_stream_source_ids() {
  std::map<std::string,std::vector<uint32_t>> result;
  for (auto app: m_session->enabled_applications()) {
    auto ro_app = app->cast<appmodel::ReadoutApplication>();
    if (ro_app != nullptr) {
      std::vector<uint32_t> streams;
      for (auto res: ro_app->contained_resources()) {
        if (!res->is_disabled(*m_session)) {
          auto d2d = res->cast<confmodel::DetectorToDaqConnection>();
          if (d2d == nullptr) {
            throw (BadD2d(ERS_HERE, app->full_name(), res->full_name()));
          }
          for (auto stream: d2d->streams()) {
            if (!stream->is_disabled(*m_session)) {
              streams.push_back(stream->get_source_id());
            }
          }
        }
      }
      result.insert(std::pair{app->UID(), streams});
    }
    else {
      auto fake_app = app->cast<appmodel::FakeDataApplication>();
      if (fake_app != nullptr) {
        std::vector<uint32_t> streams;
        for (auto res: fake_app->contained_resources()) {
          if (!res->is_disabled(*m_session)) {
            auto fdpc = res->cast<appmodel::FakeDataProdConf>();
            if (fdpc != nullptr && !fdpc->is_disabled(*m_session)) {
              streams.push_back(fdpc->get_source_id());
            }
          }
        }
        result.insert(std::pair(app->UID(), streams));
      }
    }
  }
  return result;
}

std::map<std::string, std::vector<const SourceIDConf*>>
ConfigurationHelper::get_tp_source_ids(){
  std::map<std::string, std::vector<const SourceIDConf*>> result;
  for (auto app: m_session->enabled_applications()) {
    auto ro_app = app->cast<appmodel::ReadoutApplication>();
    if (ro_app != nullptr) {
      result.insert(std::pair(app->UID(), ro_app->get_tp_source_ids()));
    }
  }
  return result;
}

std::vector<std::string> ConfigurationHelper::get_app_uids(
  std::string app_class){
  std::vector<std::string> result;
  for (auto app: m_session->enabled_applications()) {
    if (app_class.empty() || app->castable(app_class)) {
      result.push_back(app->UID());
    }
  }
  return result;
}

std::map<std::string, const SourceIDConf*>
ConfigurationHelper::get_app_source_ids(std::string app_class) {
  std::map<std::string, const SourceIDConf*> result;
  for (auto app: m_session->enabled_applications()) {
    if (app_class.empty() || app->castable(app_class)) {
      auto smart_app = app->cast<SmartDaqApplication>();
      if (smart_app != nullptr && smart_app->get_source_id() != nullptr) {
        result.insert({app->UID(), smart_app->get_source_id()});
      }
    }
  }
  return result;
}


std::map<std::string, std::map<std::string, const SourceIDConf*>>
ConfigurationHelper::get_all_app_source_ids(std::string app_class) {
  std::map<std::string, std::map<std::string, const SourceIDConf*>> result;
  for (auto app: m_session->enabled_applications()) {
    if (app_class.empty() || app->castable(app_class)) {
      auto class_info = app->configuration().get_class_info(app->class_name());
      auto obj = app->config_object();
      for (auto rel: class_info.p_relationships) {
        if (rel.p_type == "SourceIDConf") {
          if (rel.p_cardinality == dunedaq::conffwk::cardinality_t::zero_or_one ||
              rel.p_cardinality == dunedaq::conffwk::cardinality_t::only_one) {
            dunedaq::conffwk::ConfigObject rel_obj;
            obj.get(rel.p_name, rel_obj);
            if (!rel_obj.is_null()) {
              if (!result.contains(app->UID())) {
                result.insert({app->UID(), {}});
              }
              const auto srcid = app->configuration().get<SourceIDConf>(rel_obj);
              result.at(app->UID()).insert({rel.p_name, srcid});
            }
          } // cardinality
        } // SourceIDConf
      } // relationships
    } // class
  } // apps

  return result;
}

bool ConfigurationHelper::is_disabled(const conffwk::DalObject* item) {
  auto res = item->cast<confmodel::Resource>();
  if (res == nullptr) {
    return false;
  }
  return res->is_disabled(*m_session);
}
