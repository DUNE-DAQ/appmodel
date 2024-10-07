/*
 * Graph_test.cpp
 *
 *  Created on: 2 September 2024
 *      Author: John Freeman
 *
 * These unit tests are basically to test out various
 * boost::adjacency_list tools to ensure we can generate the *.dot
 * files we need to plot configurations
 *
 * Much of this is heavily based on
 * https://www.boost.org/doc/libs/1_77_0/libs/graph/doc/bundles.html
 * 
 * This is part of the DUNE DAQ Application Framework, copyright 2020.
 * Licensing/copyright details are in the COPYING file that you should have
 * received with this code.
 */

#define BOOST_TEST_MODULE Graph_test // NOLINT

#include "boost/test/unit_test.hpp"
#include "boost/graph/graph_traits.hpp"
#include "boost/graph/graphviz.hpp"
#include "boost/graph/adjacency_list.hpp"
#include "boost/graph/labeled_graph.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace boost;

BOOST_AUTO_TEST_SUITE(Graph_test)

namespace graphtest {

  struct City
  {
    std::string name;
    int population;
    std::vector<int> zipcodes;
  };

  struct Highway
  {
    std::string name;
    double miles;
    int speed_limit;
  };
  
} // namespace graphtest

BOOST_AUTO_TEST_CASE(Construct)
{
  adjacency_list<vecS, vecS, bidirectionalS, graphtest::City, graphtest::Highway> interstate;

  auto chicago = add_vertex({"Chicago", 2800000, {60647, 60622}}, interstate);
  auto nyc = add_vertex({"New York", 8300000, {10024, 10011}}, interstate);

  auto vtxs = vertices(interstate);
  BOOST_REQUIRE(std::distance(vtxs.first, vtxs.second) == 2);

  
  add_edge(chicago, nyc, {"I-80", 800, 65}, interstate);

  auto ejes = edges(interstate);
  BOOST_REQUIRE(std::distance(ejes.first, ejes.second) == 1);

  // Visually look at the DOT output, and then do a typical automated unit test on it
  
  boost::write_graphviz(std::cout,
			interstate,
			boost::make_label_writer(boost::get(&graphtest::City::name, interstate)),
			boost::make_label_writer(boost::get(&graphtest::Highway::name, interstate)));
  
  std::stringstream dotfilestream;
  boost::write_graphviz(dotfilestream,
			interstate,
			boost::make_label_writer(boost::get(&graphtest::City::name, interstate)),
			boost::make_label_writer(boost::get(&graphtest::Highway::name, interstate)));

  for (auto& token : {"New York", "Chicago", "I-80"} ) {
    BOOST_REQUIRE(dotfilestream.str().find(token) != std::string::npos);
  }
}

BOOST_AUTO_TEST_SUITE_END()
