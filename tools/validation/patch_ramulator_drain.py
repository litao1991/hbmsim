#!/usr/bin/env python3
"""Add a bounded post-trace drain entry point to Ramulator's Python binding."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


OLD = '''  void run() {
    int fe_tick = m_frontend->get_clock_ratio();
    int mem_tick = m_memory_system->get_clock_ratio();
    if (fe_tick <= 0 || mem_tick <= 0) {
      throw std::runtime_error("clock_ratio must be > 0 for both frontend and memory system");
    }

    int fe_count = mem_tick - 1, mem_count = fe_tick - 1;
    for (;;) {
      if (++fe_count >= mem_tick) {
        fe_count = 0;
        m_frontend->tick();
      }

      if (m_frontend->is_finished()) {
        break;
      }

      if (++mem_count >= fe_tick) {
        mem_count = 0;
        m_memory_system->tick();
      }
    }
  }
'''

NEW = '''  void run() {
    run_with_drain(0);
  }

  void run_with_drain(int drain_cycles) {
    if (drain_cycles < 0) {
      throw std::runtime_error("drain_cycles must be non-negative");
    }
    int fe_tick = m_frontend->get_clock_ratio();
    int mem_tick = m_memory_system->get_clock_ratio();
    if (fe_tick <= 0 || mem_tick <= 0) {
      throw std::runtime_error("clock_ratio must be > 0 for both frontend and memory system");
    }

    int fe_count = mem_tick - 1, mem_count = fe_tick - 1;
    for (;;) {
      if (++fe_count >= mem_tick) {
        fe_count = 0;
        m_frontend->tick();
      }

      if (m_frontend->is_finished()) {
        break;
      }

      if (++mem_count >= fe_tick) {
        mem_count = 0;
        m_memory_system->tick();
      }
    }
    for (int cycle = 0; cycle < drain_cycles; cycle++) {
      m_memory_system->tick();
    }
  }
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--record", type=Path, required=True)
    args = parser.parse_args()
    bindings = args.root / "src/ramulator/python/bindings.cpp"
    text = bindings.read_text(encoding="utf-8")
    already_patched = "void run_with_drain(int drain_cycles)" in text
    if not already_patched and OLD not in text:
        raise RuntimeError(f"unsupported Ramulator bindings revision: {bindings}")
    if not already_patched:
        text = text.replace(OLD, NEW, 1)
    anchor = '      .def("run", &Simulation::run, "Run the simulation to completion.")'
    replacement = anchor + '\n      .def("run_with_drain", &Simulation::run_with_drain, "Run and tick memory after input completion.")'
    if not already_patched and anchor not in text:
        raise RuntimeError("could not locate Ramulator Python run binding")
    if not already_patched:
        bindings.write_text(text.replace(anchor, replacement, 1), encoding="utf-8")
    args.record.parent.mkdir(parents=True, exist_ok=True)
    args.record.write_text(
        "Ramulator binding adapter\\n"
        "Purpose: bounded normal memory ticks after ReadWriteTrace input ends.\\n"
        "Source file: src/ramulator/python/bindings.cpp\\n"
        f"Patched SHA-256: {hashlib.sha256(bindings.read_bytes()).hexdigest()}\\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
