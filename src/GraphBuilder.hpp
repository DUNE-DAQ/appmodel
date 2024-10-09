/************************************************************
 *
 * GraphBuilder.hpp
 *
 * JCF, Sep-11-2024
 *
 * GraphBuilder is the tool we can use to plot configurations. A quick overview:
 *
 * - Constructed from an OKS database file (XML)
 * - GraphBuilder::construct_graph will take a "root object" and
 *   construct a graph accordingly
 * - GraphBuilder::write_graph will take the name of an output DOT
 *   file and write the graph to it
 *
 * The resulting DOT file can then be processed by, e.g., Graphviz in
 * order to generate a viewable graphic of the configuration
 *
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 *
 *************************************************************/

#ifndef APPMODEL_SRC_GRAPHBUILDER_HPP_
#define APPMODEL_SRC_GRAPHBUILDER_HPP_

#include "conffwk/ConfigObject.hpp"

#include "confmodel/System.hpp"
#include "ers/ers.hpp"

#include "boost/graph/graph_traits.hpp"
#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/labeled_graph.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace appmodel {

  class GraphBuilder {

  public:

    using ConfigObject = dunedaq::conffwk::ConfigObject;

    struct VertexLabel;
    struct EdgeLabel;

    // Switching container type to boost::listS seems to cause
    // compilation problems with the boost::write_graphviz function...
    using Graph_t = boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS, VertexLabel, EdgeLabel>;
    using Edge_t = boost::graph_traits<Graph_t>::edge_descriptor;
    using Vertex_t = boost::graph_traits<Graph_t>::vertex_descriptor;

    enum class ObjectKind {
      kUndefined,
      kSystem,
      kSegment,
      kApplication,
      kModule,
      kIncomingExternal,   // Object is meant to represent the outside world, not an actual component of the DAQ
      kOutgoingExternal    // ""
    };

    struct VertexLabel {

      VertexLabel() = default;
      
      VertexLabel(const std::string& uid, const std::string& classname) :
	displaylabel(uid + "\n" + classname)
      {}

      const std::string displaylabel {"undefined"};
    };

    struct EdgeLabel {
      const std::string displaylabel {"undefined"};
    };

    explicit GraphBuilder(const std::string& oksfilename);

    void construct_graph(const std::string& root_obj_uid);
    void write_graph(const std::string& outputfilename) const;
    
    GraphBuilder(const GraphBuilder&) = delete;
    GraphBuilder(GraphBuilder&&) = delete;
    GraphBuilder& operator=(const GraphBuilder&) = delete;
    GraphBuilder& operator=(GraphBuilder&&) = delete;

  private:

    struct EnhancedObject {

      struct ReceivingInfo {
	std::string connection_name;
	std::string receiver_label;

	bool operator==(const ReceivingInfo& other) const = default;
      };

      EnhancedObject(const ConfigObject& config_object_arg, ObjectKind kind_arg) :
	config_object {config_object_arg},
	kind {kind_arg}
      {}

      ConfigObject config_object;
      ObjectKind kind;

      Vertex_t vertex_in_graph;

      // What objects is this one the parent of? E.g., a parent system with child segments
      std::vector<std::string> child_object_names;

      // What objects does this one send data to, and what are their connections called?
      std::vector<ReceivingInfo> receiving_object_infos; 
    };

    void find_candidate_objects();
    [[nodiscard]] std::vector<dunedaq::conffwk::ConfigObject> find_child_objects(const ConfigObject& parent_obj);
    void calculate_graph(const std::string& root_obj_uid);
    
    void find_objects_and_connections(const ConfigObject& object);
    void calculate_network_connections();

    const std::string m_oksfilename;
    dunedaq::conffwk::Configuration* m_confdb;

    const std::unordered_map<ObjectKind, std::vector<std::string>> m_included_classes;

    std::unordered_map<std::string, EnhancedObject> m_objects_for_graph;

    std::unordered_map<std::string, std::vector<std::string>> m_incoming_connections;
    std::unordered_map<std::string, std::vector<std::string>> m_outgoing_connections;
    
    Graph_t m_graph;
    ObjectKind m_root_object_kind;

    dunedaq::confmodel::System* m_system;
    std::string m_system_name;
    
    std::vector<std::string> m_ignored_application_uids;
    
    std::vector<ConfigObject> m_all_objects;
    std::vector<ConfigObject> m_candidate_objects;
  };

  [[nodiscard]] constexpr GraphBuilder::ObjectKind get_object_kind(const std::string& class_name);

} // namespace appmodel


ERS_DECLARE_ISSUE(appmodel,                 
                  GeneralGraphToolError,
                  "A graph tool error occured: " << errmsg,
                  ((std::string)errmsg)
)


#endif // APPMODEL_SRC_GRAPHBUILDER_HPP_
