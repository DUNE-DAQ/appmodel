/************************************************************
 *
 * GraphBuilder.cpp
 *
 * JCF, Sep-11-2024
 *
 * Implementation of GraphBuilder::construct_graph and GraphBuilder::write_graph
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 *
 *************************************************************/

#include "GraphBuilder.hpp"

#include "appmodel/DFApplication.hpp"
#include "appmodel/DFOApplication.hpp"
#include "appmodel/ReadoutApplication.hpp"
#include "appmodel/SmartDaqApplication.hpp"
#include "appmodel/TriggerApplication.hpp"
#include "appmodel/MLTApplication.hpp"
#include "appmodel/TPStreamWriterApplication.hpp"
#include "appmodel/appmodelIssues.hpp"

#include "conffwk/Configuration.hpp"
#include "conffwk/Schema.hpp"
#include "confmodel/Session.hpp"
#include "confmodel/Connection.hpp"
#include "confmodel/DaqModule.hpp"
#include "ers/ers.hpp"

#include "boost/graph/graphviz.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace appmodel {

  GraphBuilder::GraphBuilder(const std::string& oksfilename, const std::string& sessionname) :
    m_oksfilename { oksfilename },
    m_confdb { nullptr },
    m_included_classes {
      { ObjectKind::kSession, {"Session", "Segment", "Application" } },
      { ObjectKind::kSegment, {"Segment", "Application"} },
      { ObjectKind::kApplication, {"Application", "Module"} },
      { ObjectKind::kModule, {"Module"} }
    },
    m_root_object_kind { ObjectKind::kUndefined },
    m_session { nullptr },
    m_session_name { sessionname }
  {

    // Open the database represented by the OKS XML file

    try {
      m_confdb = new dunedaq::conffwk::Configuration("oksconflibs:" + m_oksfilename);
    } catch (dunedaq::conffwk::Generic& exc) {
      TLOG() << "Failed to load OKS database: " << exc << "\n";
      throw exc;
    }

    // Get the session in the database
    std::vector<ConfigObject> session_objects {};

    m_confdb->get("Session", session_objects);

    if (m_session_name == "") { // If no session name given, use the one-and-only session expected in the database
      if (session_objects.size() == 1) {
	m_session_name = session_objects[0].UID();
      } else {
	std::stringstream errmsg;
	errmsg << "No Session instance name was provided, and since " << session_objects.size() << " session instances were found in \"" << m_oksfilename << "\" this is an error";

	throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
      }
    } else { // session name provided by the user, let's make sure it's there
      auto it = std::ranges::find_if(session_objects, [&](const ConfigObject& obj) { return obj.UID() == m_session_name; });

      if (it == session_objects.end()) {
	std::stringstream errmsg;
	errmsg << "Did not find Session instance \"" << m_session_name << "\" in \"" << m_oksfilename << "\" and its includes";
	throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
      }
    }

    // The following not-brief section of code is dedicated to
    // determining which applications in the configuration are
    // disabled
    
    // First, we need the session object to check if an application
    // has been disabled

    // Note the "const_cast" is needed since "m_confdb->get"
    // returns a const pointer, but since m_session is a member needed
    // by multiple functions and can't be determined until after we've
    // opened the database and found the session, we need to change
    // its initial value here. Once this is done, it shouldn't be
    // changed again.

    m_session = const_cast<dunedaq::confmodel::Session*>(  // NOLINT
                                                         m_confdb->get<dunedaq::confmodel::Session>(m_session_name));
  
    if (!m_session) {
      std::stringstream errmsg;
      errmsg << "Unable to get session with UID \"" << m_session_name << "\"";
      throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
    }

    std::vector<ConfigObject> every_object_deriving_from_class {}; // Includes objects of the class itself
    std::vector<ConfigObject> objects_of_class {}; // A subset of every_object_deriving_from_class
    
    // m_confdb->superclasses() returns a conffwk::fmap; see the conffwk package for details
    
    auto classnames = m_confdb->superclasses() | std::views::keys |
      std::views::transform([](const auto& ptr_to_class_name) {
        return *ptr_to_class_name;
      });

    for (const auto& classname : classnames) {

      every_object_deriving_from_class.clear();
      objects_of_class.clear();

      m_confdb->get(classname, every_object_deriving_from_class);

      std::ranges::copy_if(every_object_deriving_from_class,
                           std::back_inserter(objects_of_class),
                           [&classname](const ConfigObject& obj) {
                             return obj.class_name() == classname;
                           });

      std::ranges::copy(objects_of_class, std::back_inserter(m_all_objects));

      if (classname.find("Application") != std::string::npos ) { // DFApplication, ReadoutApplication, etc.
        for (const auto& appobj : objects_of_class) {

          auto daqapp = m_confdb->get<dunedaq::appmodel::SmartDaqApplication>(appobj.UID());

          if (daqapp) {

            auto res = daqapp->cast<dunedaq::confmodel::ResourceBase>();
            
            if (res && res->disabled(*m_session)) {
              m_ignored_application_uids.push_back( appobj.UID() );
              TLOG() << "Skipping disabled application " << appobj.UID() << "@" << daqapp->class_name();
              continue;
            }
          } else {
            TLOG(TLVL_DEBUG) << "Skipping non-SmartDaqApplication " << appobj.UID() << "@" << appobj.class_name();
            m_ignored_application_uids.push_back( appobj.UID() );
          }
        }
      }
    }
  }

  void GraphBuilder::find_candidate_objects() {

    m_candidate_objects.clear();

    for (const auto& obj: m_all_objects) {
      for (const auto& classname : this->m_included_classes.at(m_root_object_kind)) {

        if (obj.class_name().find(classname) != std::string::npos &&
            std::ranges::find(m_ignored_application_uids, obj.UID()) == m_ignored_application_uids.end()) {
          m_candidate_objects.emplace_back(obj);
        }
      }
    }
  }

  void GraphBuilder::calculate_graph(const std::string& root_obj_uid) {

    // To start, get the session / segments / applications in the
    // session by setting up a temporary graph with the session as its
    // root. This way we can check to see if the actual requested root
    // object lies within the session in question.

    auto true_root_object_kind = m_root_object_kind;
    m_root_object_kind = ObjectKind::kSession;
    find_candidate_objects();

    auto it_session = std::ranges::find_if(m_all_objects, [&](const ConfigObject& obj) { return obj.UID() == m_session_name; });

    find_objects_and_connections(*it_session);

    if (!m_objects_for_graph.contains(root_obj_uid)) {
      std::stringstream errmsg;
      errmsg << "Unable to find requested object \"" << root_obj_uid << "\" in session \"" << m_session_name << "\"";
      throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
    }

    // Since we used our first call to find_objects_and_connections
    // only as a fact-finding mission, reset the containers it filled

    m_objects_for_graph.clear();
    m_incoming_connections.clear();
    m_outgoing_connections.clear();

    m_candidate_objects.clear();

    auto m_root_object_kind = true_root_object_kind;
    find_candidate_objects();

    bool found = false;
    for (auto& obj: m_candidate_objects) {
      if (obj.UID() == root_obj_uid) {
        found = true;
        find_objects_and_connections(obj);   // Put differently, "find what will make up the vertices and edges"
        break;
      }
    }

    assert(found);

    calculate_network_connections();  // Put differently, "find the edges between the vertices"
  }

  void GraphBuilder::calculate_network_connections() {

    // Will use "incoming_matched" and "outgoing_matched" to keep
    // track of incoming and outgoing connections which don't get
    // matched, i.e. would terminate external to the graph

    std::vector<std::string> incoming_matched;
    std::vector<std::string> outgoing_matched;

    for (auto& incoming : m_incoming_connections) {

      std::regex incoming_pattern(incoming.first);

      for (auto& outgoing : m_outgoing_connections) {

        std::regex outgoing_pattern(outgoing.first);

        bool match = false;

        if (incoming.first == outgoing.first) {
          match = true;
        } else if (incoming.first.find(".*") != std::string::npos) {
          if (std::regex_match(outgoing.first, incoming_pattern)) {
            match = true;
          }
        } else if (outgoing.first.find(".*") != std::string::npos) {
          if (std::regex_match(incoming.first, outgoing_pattern)) {
            match = true;
          }
        }

        if (match) {

          incoming_matched.push_back(incoming.first);
          outgoing_matched.push_back(outgoing.first);

          for (auto& receiver : incoming.second) {
            for (auto& sender : outgoing.second) {

              // We just want to plot applications sending to other
              // applications and queues sending to other
              // queues. Showing, e.g., a queue directly sending to
              // some other application via a network connection makes
              // the plot too busy.

              if (!m_objects_for_graph.contains(sender) || !m_objects_for_graph.contains(receiver)) {
                continue;
              } else if (m_objects_for_graph.at(sender).kind != m_objects_for_graph.at(receiver).kind) {
                continue;
              }

              const EnhancedObject::ReceivingInfo receiving_info {incoming.first, receiver};

              auto res = std::ranges::find(m_objects_for_graph.at(sender).receiving_object_infos, receiving_info );
              if (res == m_objects_for_graph.at(sender).receiving_object_infos.end() ) {
                m_objects_for_graph.at(sender).receiving_object_infos.push_back(receiving_info);
              }
            }
          }
        }
      }
    }

    auto incoming_unmatched = m_incoming_connections | std::views::keys | std::views::filter(
      [&incoming_matched](auto& connection) {
        return std::ranges::find(incoming_matched, connection) == incoming_matched.end();
      });

    auto included_classes = m_included_classes.at(m_root_object_kind);

    for (auto& incoming_conn : incoming_unmatched) {

      EnhancedObject external_obj {ConfigObject{}, ObjectKind::kIncomingExternal};
      const std::string incoming_vertex_name = incoming_conn;

      // Find the connections appropriate to the granularity level of this graph
      for (auto& receiving_object_name : m_incoming_connections[incoming_conn]) {

        if (!m_objects_for_graph.contains(receiving_object_name)) {
          continue;
        }

        if (std::ranges::find(included_classes, "Module") != included_classes.end()) {
          if (m_objects_for_graph.at(receiving_object_name).kind == ObjectKind::kModule) {
            external_obj.receiving_object_infos.push_back( {incoming_conn, receiving_object_name} );
          }
        } else if (std::ranges::find(included_classes, "Application") != included_classes.end()) {
          if (m_objects_for_graph.at(receiving_object_name).kind == ObjectKind::kApplication) {
            external_obj.receiving_object_infos.push_back( {incoming_conn, receiving_object_name} );
          }
        }
      }

      m_objects_for_graph.insert( {incoming_vertex_name, external_obj} );
    }

    auto outgoing_unmatched = m_outgoing_connections | std::views::keys | std::views::filter(
      [&outgoing_matched](auto& connection) {
        return std::ranges::find(outgoing_matched, connection) == outgoing_matched.end();
      });

    for (auto& outgoing_conn : outgoing_unmatched) {

      EnhancedObject external_obj {ConfigObject{}, ObjectKind::kOutgoingExternal};
      const std::string outgoing_vertex_name = outgoing_conn;

      // Find the connections appropriate to the granularity level of this graph
      for (auto& sending_object_name : m_outgoing_connections[outgoing_conn]) {

        if (!m_objects_for_graph.contains(sending_object_name)) {
          continue;
        }

        if (std::ranges::find(included_classes, "Module") != included_classes.end()) {
          if (m_objects_for_graph.at(sending_object_name).kind == ObjectKind::kModule) {
            m_objects_for_graph.at(sending_object_name).receiving_object_infos.push_back( {outgoing_conn, outgoing_vertex_name} );
          }
        } else if (std::ranges::find(included_classes, "Application") != included_classes.end()) {
          if (m_objects_for_graph.at(sending_object_name).kind == ObjectKind::kApplication) {
            m_objects_for_graph.at(sending_object_name).receiving_object_infos.push_back( {outgoing_conn, outgoing_vertex_name} );
          }
        }
      }

      m_objects_for_graph.insert( {outgoing_vertex_name, external_obj} );
    }
  }

  void
  GraphBuilder::find_objects_and_connections(const ConfigObject& object) {

    EnhancedObject starting_object { object, get_object_kind(object.class_name()) };

    // If we've got a session or a segment, look at its OKS-relations,
    // and recursively process those relation objects which are on the
    // candidates list and haven't already been processed

    if (starting_object.kind == ObjectKind::kSession || starting_object.kind ==	ObjectKind::kSegment) {

      for (auto& child_object: find_child_objects(starting_object.config_object)) {

        if (std::ranges::find(m_candidate_objects, child_object) != m_candidate_objects.end()) {
          find_objects_and_connections(child_object);
          starting_object.child_object_names.push_back(child_object.UID());
        }
      }
    } else if (starting_object.kind == ObjectKind::kApplication) {

      // If we've got an application object, try to determine what
      // modules are in it and what their connections are. Recursively
      // process the modules, and then add connection info to class-wide
      // member maps to calculate edges corresponding to the connections
      // for the plotted graph later

      dunedaq::conffwk::Configuration* local_database {nullptr};

      try {                                                                                                
        local_database = new dunedaq::conffwk::Configuration("oksconflibs:" + m_oksfilename);               
      } catch (dunedaq::conffwk::Generic& exc) {                                                           
        TLOG() << "Failed to load OKS database: " << exc << "\n";                                          
        throw exc;                                                                                         
      }

      auto daqapp = local_database->get<dunedaq::appmodel::SmartDaqApplication>(object.UID());                 
      if (daqapp) {
        auto local_session = const_cast<dunedaq::confmodel::Session*>(  // NOLINT
                                                                      local_database->get<dunedaq::confmodel::Session>(m_session_name));
        auto modules = daqapp->generate_modules(local_database, m_oksfilename, local_session);

        std::vector<std::string> allowed_conns {};

        if (m_root_object_kind == ObjectKind::kSession || m_root_object_kind == ObjectKind::kSegment) {
          allowed_conns = {"NetworkConnection"};
        } else if (m_root_object_kind == ObjectKind::kApplication || m_root_object_kind == ObjectKind::kModule){
          allowed_conns = {"NetworkConnection", "Queue", "QueueWithSourceId"};
        }

        for (const auto& module : modules) {

          for (auto in : module->get_inputs()) {

            // Elsewhere in the code it'll be useful to know if the
            // connection is a network or a queue, so include the
            // class name in the std::string key

            const std::string key = in->config_object().UID() + "@" + in->config_object().class_name();

            if (std::ranges::find(allowed_conns, in->config_object().class_name()) != allowed_conns.end()) {
              m_incoming_connections[key].push_back( object.UID() );
              m_incoming_connections[key].push_back( module->UID() );
            }
          }

          for (auto out : module->get_outputs()) {

            const std::string key = out->config_object().UID() + "@" + out->config_object().class_name();

            if (std::ranges::find(allowed_conns, out->config_object().class_name()) != allowed_conns.end()) {
              m_outgoing_connections[key].push_back( object.UID() );
              m_outgoing_connections[key].push_back( module->UID() );
            }
          }

          if (std::ranges::find(m_included_classes.at(m_root_object_kind), "Module") != m_included_classes.at(m_root_object_kind).end()) {
            find_objects_and_connections(module->config_object());
            starting_object.child_object_names.push_back(module->UID());
          }
        }
      }
    }

    assert(!m_objects_for_graph.contains(object.UID()));

    m_objects_for_graph.insert( {object.UID(), starting_object} );
  }

  void GraphBuilder::construct_graph(const std::string& root_obj_uid) {

    // Next several lines just mean "tell me the class type of the root object in the config plot's graph"

    auto class_names_view = m_all_objects |
      std::views::filter([root_obj_uid](const ConfigObject& obj) {
        return obj.UID() == root_obj_uid;
      }) |
      std::views::transform([](const ConfigObject& obj){
        return obj.class_name();
      });

    if (std::ranges::distance(class_names_view) != 1) {
      std::stringstream errmsg;
      errmsg << "Failed to find instance of desired root object \"" << root_obj_uid << "\"";
      throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
    }

    const std::string& root_obj_class_name = *class_names_view.begin();

    m_root_object_kind = get_object_kind(root_obj_class_name);

    calculate_graph(root_obj_uid);
    
    for (auto& enhanced_object : m_objects_for_graph | std::views::values) {

      if (enhanced_object.kind == ObjectKind::kIncomingExternal) {
        enhanced_object.vertex_in_graph = boost::add_vertex(VertexLabel("O", ""), m_graph);
      } else if (enhanced_object.kind == ObjectKind::kOutgoingExternal) {
        enhanced_object.vertex_in_graph = boost::add_vertex(VertexLabel("X", ""), m_graph);
      } else {
        auto& obj = enhanced_object.config_object;
        enhanced_object.vertex_in_graph = boost::add_vertex( VertexLabel(obj.UID(), obj.class_name()), m_graph);
      } 
    }

    for (auto& parent_obj : m_objects_for_graph | std::views::values) {
      for (auto& child_obj_name : parent_obj.child_object_names) {
        boost::add_edge(parent_obj.vertex_in_graph,
                        m_objects_for_graph.at(child_obj_name).vertex_in_graph,
                        {""}, // No label for an edge which just describes "ownership" rather than dataflow
                        m_graph);
      }
    }

    for (auto& possible_sender_object : m_objects_for_graph | std::views::values) {
      for (auto& receiver_info : possible_sender_object.receiving_object_infos) {

        auto at_pos = receiver_info.connection_name.find("@");
        const std::string connection_label = receiver_info.connection_name.substr(0, at_pos);

        // If we're plotting at the level of a session or segment,
        // show the connections as between applications; if we're
        // doing this for a single application, show them entering and
        // exiting the individual modules

        if (m_root_object_kind == ObjectKind::kSession || m_root_object_kind == ObjectKind::kSegment) {
          if (m_objects_for_graph.at(receiver_info.receiver_label).kind == ObjectKind::kModule) {
            continue;
          }
        }

        if (m_root_object_kind == ObjectKind::kApplication || m_root_object_kind == ObjectKind::kModule) {
          if (m_objects_for_graph.at(receiver_info.receiver_label).kind == ObjectKind::kApplication) {
            continue;
          }
        }

        boost::add_edge(
                        possible_sender_object.vertex_in_graph,
                        m_objects_for_graph.at(receiver_info.receiver_label).vertex_in_graph,
                        { connection_label },
                        m_graph).first;
      }
    }
  }

  std::vector<dunedaq::conffwk::ConfigObject>
  GraphBuilder::find_child_objects(const ConfigObject& parent_obj) {

    std::vector<ConfigObject> connected_objects {};

    dunedaq::conffwk::class_t classdef = m_confdb->get_class_info( parent_obj.class_name(), false );

    for (const dunedaq::conffwk::relationship_t& relationship : classdef.p_relationships) {

      // The ConfigObject::get(...) function doesn't have a
      // const-qualifier on it for no apparent good reason; we need
      // this cast in order to call it
      
      auto parent_obj_casted = const_cast<ConfigObject&>(parent_obj); // NOLINT
      
      if (relationship.p_cardinality == dunedaq::conffwk::only_one ||
          relationship.p_cardinality == dunedaq::conffwk::zero_or_one) {
        ConfigObject connected_obj {};
        parent_obj_casted.get(relationship.p_name, connected_obj);
        connected_objects.push_back(connected_obj);
      } else {
        std::vector<ConfigObject> connected_objects_in_relationship {};
        parent_obj_casted.get(relationship.p_name, connected_objects_in_relationship);
        connected_objects.insert(connected_objects.end(), connected_objects_in_relationship.begin(), connected_objects_in_relationship.end());
      }
    }

    return connected_objects;
  }

  void GraphBuilder::write_graph(const std::string& outputfilename) const {

    std::stringstream outputstream;

    boost::write_graphviz(outputstream,
                          m_graph,
                          boost::make_label_writer(boost::get(&GraphBuilder::VertexLabel::displaylabel, m_graph)),
                          boost::make_label_writer(boost::get(&GraphBuilder::EdgeLabel::displaylabel, m_graph))
                          );

    // It's arguably hacky to edit the DOT code generated by
    // boost::write_graphviz after the fact to give vertices colors,
    // but the fact is that the color-assigning logic in Boost's graph
    // library is so messy and clumsy that this is a worthwhile
    // tradeoff

    struct VertexStyle {
      const std::string shape;
      const std::string color;
    };

    const std::unordered_map<ObjectKind, VertexStyle> vertex_styles {
      {ObjectKind::kSession, {"octagon", "black"}},
      {ObjectKind::kSegment, {"hexagon", "brown"}},
      {ObjectKind::kApplication, {"pentagon", "blue"}},
      {ObjectKind::kModule, {"rectangle", "red"}}
    };

    std::string dotfile_slurped = outputstream.str();
    std::vector<std::string> legend_entries {};
    std::vector<std::string> legend_ordering_code {};

    for (auto& eo : m_objects_for_graph | std::views::values) {

      std::stringstream vertexstr {};
      std::stringstream legendstr {};
      std::stringstream labelstringstr {};
      size_t insertion_location {0};

      auto calculate_insertion_location = [&](){

        labelstringstr << "label=\"" << eo.config_object.UID() << "\n";	  
        insertion_location = dotfile_slurped.find(labelstringstr.str());
        assert(insertion_location != std::string::npos);
        return insertion_location;
      };

      // TODO: John Freeman (jcfree@fnal.gov), Sep-17-2024

      // Switch to std::format for line construction rather than
      // std::stringstream when we switch to a gcc version which
      // supports it

      auto add_vertex_info = [&]() {
        vertexstr << "shape=" << vertex_styles.at(eo.kind).shape << ", color=" <<
          vertex_styles.at(eo.kind).color << ", fontcolor=" << vertex_styles.at(eo.kind).color << ", ";
        dotfile_slurped.insert(calculate_insertion_location(), vertexstr.str());
      };

      auto add_legend_entry = [&](char letter, const std::string objkind) {
        legendstr << "legend" << letter << " [label=<<font color=\"" << vertex_styles.at(eo.kind).color << "\"><b><i>" << vertex_styles.at(eo.kind).color << ": " << objkind << "</i></b></font>>, shape=plaintext, color=" << vertex_styles.at(eo.kind).color << ", fontcolor=" << vertex_styles.at(eo.kind).color << "];";
      };
      
      // Note that the seemingly arbitrary single characters added
      // after "legend" aren't just to uniquely identify each entry in
      // the legend, it's also so that when the entries are sorted
      // alphabetically they'll appear in the correct order
      
      switch (eo.kind) {
      case ObjectKind::kSession:
        add_vertex_info();
        add_legend_entry('A', "session");
        break;
      case ObjectKind::kSegment:
        add_vertex_info();
        add_legend_entry('B', "segment");
        break;
      case ObjectKind::kApplication:
        add_vertex_info();
        add_legend_entry('C', "application");
        break;
      case ObjectKind::kModule:
        add_vertex_info();
        add_legend_entry('D', "DAQModule");
        break;
      case ObjectKind::kIncomingExternal:
        legendstr << "legendE [label=<<font color=\"black\">O:<b><i> External Data Source</i></b></font>>, shape=plaintext];";
        break;
      case ObjectKind::kOutgoingExternal:
        legendstr << "legendF [label=<<font color=\"black\">X:<b><i> External Data Sink</i></b></font>>, shape=plaintext];";
        break;
      default:
        assert(false);
      }

      if (std::ranges::find(legend_entries, legendstr.str()) == legend_entries.end()) {
        legend_entries.emplace_back(legendstr.str());
      }
    }

    std::ranges::sort(legend_entries);

    // We have the line-by-line entries containing the labels in our
    // legend and their colors, but more DOT code is needed in order
    // to show the labels in order. E.g., if we have:

    // legendA [label=<<font color="blue">Blue: Segment</font>>, shape=box, color=blue, fontcolor=blue];
    // legendB [label=<<font color="red">Red: Application</font>>, shape=box, color=red, fontcolor=red];
    //
    // Then we'll also need
    //
    // legendA -> legendB [style=invis];

    auto legend_tokens = legend_entries | std::views::transform([](const std::string& line) {
      return line.substr(0, line.find(' ')); // i.e., grab the first word on the line
    });

    auto it = legend_tokens.begin();
    for (auto next_it = std::next(it); next_it != legend_tokens.end(); ++it, ++next_it) {
      std::stringstream astr {};
      astr << "        " << *it << " -> " << *next_it << " [style=invis];";
      legend_ordering_code.push_back(astr.str());
    }

    constexpr int chars_to_last_brace = 2;
    auto last_brace_iter = dotfile_slurped.end() - chars_to_last_brace;
    assert(*last_brace_iter == '}');
    size_t last_brace_loc = last_brace_iter - dotfile_slurped.begin();

    std::string legend_code {};
    legend_code += "\n\n\n";

    for (const auto& l : legend_entries) {
      legend_code += l + "\n";
    }
    
    legend_code += "\n\n\n";

    for (const auto& l : legend_ordering_code) {
      legend_code += l + "\n";
    }

    dotfile_slurped.insert(last_brace_loc, legend_code);

    // Take advantage of the fact that the edges describing ownership
    // rather than data flow (e.g., hsi-segment owning hsi-01, the
    // FakeHSIApplication) have null labels in order to turn them into
    // arrow-free dotted lines

    const std::string unlabeled_edge = "label=\"\"";
    const std::string edge_modifier = ", style=\"dotted\", arrowhead=\"none\"";

    size_t pos = 0;
    while ((pos = dotfile_slurped.find(unlabeled_edge, pos)) != std::string::npos) {
      dotfile_slurped.replace(pos, unlabeled_edge.length(), unlabeled_edge + edge_modifier);
      pos += (unlabeled_edge + edge_modifier).length();
    }

    // And now with all the edits made to the contents of the DOT code, write it to file

    std::ofstream outputfile;
    outputfile.open( outputfilename );
    
    if ( outputfile.is_open() ) {
      outputfile << dotfile_slurped.c_str();
    } else {
      std::stringstream errmsg;
      errmsg << "Unable to open requested file \"" << outputfilename << "\" for writing";
      throw appmodel::GeneralGraphToolError(ERS_HERE, errmsg.str());
    }
  }

  constexpr GraphBuilder::ObjectKind get_object_kind(const std::string& class_name) {

    using ObjectKind = GraphBuilder::ObjectKind;

    ObjectKind kind = ObjectKind::kSession;

    if (class_name.find("Session") != std::string::npos ) {
      kind = ObjectKind::kSession;
    } else if (class_name.find("Segment") != std::string::npos ) {
      kind = ObjectKind::kSegment;
    } else if (class_name.find("Application") != std::string::npos ) {
      kind = ObjectKind::kApplication;
    } else if (class_name.find("Module") != std::string::npos ) {
      kind = ObjectKind::kModule;
    } else {
      throw appmodel::GeneralGraphToolError(ERS_HERE, "Unsupported class type \"" + std::string(class_name) + "\"passed to get_object_kind");
    }

    return kind;
  }

} // namespace appmodel
