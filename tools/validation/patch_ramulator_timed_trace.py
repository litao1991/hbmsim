#!/usr/bin/env python3
"""Patch the pinned Ramulator ReadWriteTrace for auditable timed validation.

The upstream frontend accepts one untimed request per tick and only reports
aggregate statistics.  This narrowly scoped adapter keeps its controller and
DRAM model intact while adding absolute arrivals plus a callback-derived CSV.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


TRACE_STRUCT = """  struct Trace {
    bool is_write;
    AddrVec_t addr_vec;
  };
"""

TRACE_STRUCT_REPLACEMENT = """  struct Trace {
    Clk_t arrival;
    bool is_write;
    size_t id;
    AddrVec_t addr_vec;
  };
"""

MEMBERS = """  size_t m_trace_length = 0;
  size_t m_curr_trace_idx = 0;
  size_t m_trace_count = 0;
  std::string m_trace_path;
"""

MEMBERS_REPLACEMENT = """  size_t m_trace_length = 0;
  size_t m_curr_trace_idx = 0;
  size_t m_completed_count = 0;
  std::string m_trace_path;
  std::string m_completion_path;
  std::ofstream m_completion_file;
"""

INIT = """    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_trace_path, std::string, "path").required();

    m_logger.info(fmt::format("Loading trace file {} ...", m_trace_path));
    init_trace(m_trace_path);
    m_logger.info(fmt::format("Loaded {} lines.", m_trace.size()));
"""

INIT_REPLACEMENT = """    RAMULATOR_PARSE_PARAM(m_clock_ratio, unsigned int, "clock_ratio").required();
    RAMULATOR_PARSE_PARAM(m_trace_path, std::string, "path").required();
    RAMULATOR_PARSE_PARAM(m_completion_path, std::string, "completion_path").required();

    m_completion_file.open(m_completion_path);
    if (!m_completion_file.is_open()) {
      throw std::runtime_error(fmt::format("Cannot open completion file {}", m_completion_path));
    }
    m_completion_file << "id,op,arrival_cycle,completion_cycle,latency_cycles\\n";
    m_logger.info(fmt::format("Loading trace file {} ...", m_trace_path));
    init_trace(m_trace_path);
    m_logger.info(fmt::format("Loaded {} lines.", m_trace.size()));
"""

TICK = """  void tick() override {
    const Trace& t = m_trace[m_curr_trace_idx];
    Request req(t.addr_vec, t.is_write ? Request::Type::Write : Request::Type::Read);
    req.size_bytes = m_memory_system->get_tx_bytes();
    bool sent = m_memory_system->send(req);
    if (sent) {
      m_curr_trace_idx = (m_curr_trace_idx + 1) % m_trace_length;
      m_trace_count++;
    }
  };
"""

TICK_REPLACEMENT = """  void tick() override {
    if (m_curr_trace_idx < m_trace_length) {
      const Trace& t = m_trace[m_curr_trace_idx];
      if (t.arrival <= m_clk) {
        Request req(t.addr_vec, t.is_write ? Request::Type::Write : Request::Type::Read);
        req.size_bytes = m_memory_system->get_tx_bytes();
        req.callback = [this, id = t.id, is_write = t.is_write, arrival = t.arrival](Request& completed) {
          // Ramulator currently invokes write callbacks at command retirement
          // without filling Request::depart.  The frontend and memory clocks
          // are both 1:1 in this validation profile, so m_clk is that
          // completion cycle for writes; reads retain their explicit depart.
          const auto completion = completed.depart >= arrival ? completed.depart : m_clk;
          const auto latency = completion - arrival;
          m_completion_file << id << ',' << (is_write ? "WRITE" : "READ") << ','
                            << arrival << ',' << completion << ',' << latency << '\\n';
          m_completion_file.flush();
          ++m_completed_count;
        };
        if (m_memory_system->send(req)) {
          ++m_curr_trace_idx;
        }
      }
    }
    ++m_clk;
  };
"""

PARSER = """      if (tokens.size() != 2) {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: expected 2 tokens, got {}", file_path_str, line_num, tokens.size()));
      }

      bool is_write = false;
      if (tokens[0] == "R") {
        is_write = false;
      } else if (tokens[0] == "W") {
        is_write = true;
      } else {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: unknown type '{}' (expected R or W)", file_path_str, line_num, tokens[0]));
      }

      std::vector<std::string> addr_vec_tokens;
      tokenize(addr_vec_tokens, tokens[1], ",");
"""

PARSER_REPLACEMENT = """      if (tokens.size() != 4) {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: expected 4 tokens, got {}", file_path_str, line_num, tokens.size()));
      }

      const auto arrival = static_cast<Clk_t>(std::stoll(tokens[0]));
      bool is_write = false;
      if (tokens[1] == "R") {
        is_write = false;
      } else if (tokens[1] == "W") {
        is_write = true;
      } else {
        throw std::runtime_error(
            fmt::format("Trace {} line {}: unknown type '{}' (expected R or W)", file_path_str, line_num, tokens[1]));
      }
      const auto id = static_cast<size_t>(std::stoull(tokens[2]));

      std::vector<std::string> addr_vec_tokens;
      tokenize(addr_vec_tokens, tokens[3], ",");
"""

PUSH = """      m_trace.push_back({is_write, addr_vec});
"""
PUSH_REPLACEMENT = """      m_trace.push_back({arrival, is_write, id, addr_vec});
"""

FINISHED = """  bool is_finished() override {
    return m_trace_count >= m_trace_length;
  };
"""
FINISHED_REPLACEMENT = """  bool is_finished() override {
    return m_curr_trace_idx >= m_trace_length && m_completed_count >= m_trace_length;
  };
"""


def replace_once(text: str, old: str, new: str, description: str) -> str:
    if old not in text:
        raise RuntimeError(f"unsupported Ramulator revision: missing {description}")
    return text.replace(old, new, 1)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--record", type=Path, required=True)
    args = parser.parse_args()
    source = args.root / "src/ramulator/frontend/impl/memory_trace/readwrite_trace.cpp"
    text = source.read_text(encoding="utf-8")
    if "completion_path" not in text:
        for old, new, description in (
            (TRACE_STRUCT, TRACE_STRUCT_REPLACEMENT, "trace structure"),
            (MEMBERS, MEMBERS_REPLACEMENT, "trace state"),
            (INIT, INIT_REPLACEMENT, "frontend initialization"),
            (TICK, TICK_REPLACEMENT, "frontend tick"),
            (PARSER, PARSER_REPLACEMENT, "trace parser"),
            (PUSH, PUSH_REPLACEMENT, "trace append"),
            (FINISHED, FINISHED_REPLACEMENT, "completion termination"),
        ):
            text = replace_once(text, old, new, description)
        source.write_text(text, encoding="utf-8")
    args.record.parent.mkdir(parents=True, exist_ok=True)
    args.record.write_text(
        "Ramulator timed-trace validation adapter\n"
        "Purpose: preserve absolute arrivals and write callback-derived completions.\n"
        "Source file: src/ramulator/frontend/impl/memory_trace/readwrite_trace.cpp\n"
        f"Patched SHA-256: {hashlib.sha256(source.read_bytes()).hexdigest()}\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
