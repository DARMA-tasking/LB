/*
//@HEADER
// *****************************************************************************
//
//                                file_reader.h
//                 DARMA/vt-lb => Virtual Transport/Load Balancers
//
// Copyright 2019-2024 National Technology & Engineering Solutions of Sandia, LLC
// (NTESS). Under the terms of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// * Neither the name of the copyright holder nor the names of its
//   contributors may be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact darma@sandia.gov
//
// *****************************************************************************
//@HEADER
*/

#if !defined INCLUDED_VT_LB_INPUT_FILE_READER_H
#define INCLUDED_VT_LB_INPUT_FILE_READER_H

#include <vt-lb/model/PhaseData.h>

#include <string>
#include <memory>
#include <optional>

namespace vt_lb::input {

struct EntityDesc {
  // Identification and placement
  std::optional<int> id;
  std::optional<int> seq_id;
  std::optional<int> collection_id;
  std::optional<int> home;
  std::optional<int> objgroup_id;
  bool migratable = false;
  std::string type;
  std::vector<int> index;
};
struct SubphaseDesc { int id; double time; };
// template<typename T>
struct TaskDesc {
  EntityDesc entity;
  int node;
  std::string resource;
  double time;
  std::vector<SubphaseDesc> subphases;
  // T attributes;     // optional dict
  // T user_defined;   // optional dict
};
struct CommDesc {
  EntityDesc to;
  EntityDesc from;
  std::string type;
  int messages;
  double bytes;
};

/**
 * \struct ReadingBehavior
 *
 * \brief Abstract base struct for reading behavior based on file type
 */
struct ReadingBehavior {
  ReadingBehavior() {};
  virtual ~ReadingBehavior() = default;
  /**
   * \brief Read a given file
   *
   * \param[in] in_filename the file name to read
   */
  virtual void readFile(std::string const& in_filename) = 0;
  /**
   * \brief Parse the data into vt-tv's data structure Info, with a single rank
   * filled out
   *
   * \param[in] phase the phase to parse
   */
  virtual void parse(int phase) = 0;
  std::vector<TaskDesc> tasks;
  std::vector<CommDesc> comms;
};

/**
 * \struct FileReader
 *
 * \brief Reader for various formats (JSON, YAML) in the LBDataType format.
 */
struct FileReader {
  /**
   * \brief Construct the reader
   *
   * \param[in] in_rank the rank for which to read data
   * \param[in] in_filename the file to be read
   */
  FileReader(int in_rank, std::string const& in_filename);

  /**
   * \brief Parse the data into vt-tv's data structure Info, with a single rank
   * filled out
   *
   * \param[in] phase the phase to parse
   */
  std::unique_ptr<model::PhaseData> parse(int phase);
private:
  int rank_ = 0;
  std::unique_ptr<ReadingBehavior> read_behavior = nullptr;
};

} /* end namespace vt_lb::input */

#endif /*INCLUDED_VT_LB_INPUT_FILE_READER_H*/

