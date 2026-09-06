#include "hbmsim/hbm_system.h"

#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_csv(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream(line);
  std::string field;
  while (std::getline(stream, field, ',')) {
    fields.push_back(field);
  }
  return fields;
}

std::uint64_t parse_u64(const std::string& value) {
  std::size_t consumed = 0;
  return std::stoull(value, &consumed, 0);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: hbmsim TRACE.csv\n"
                 "columns: arrival_ps,op,address,size_bytes[,client]\n";
    return 2;
  }

  std::ifstream trace(argv[1]);
  if (!trace) {
    std::cerr << "cannot open trace: " << argv[1] << '\n';
    return 2;
  }

  hbmsim::HbmSystem system;
  std::string line;
  std::uint64_t line_number = 0;
  std::uint64_t id = 1;
  try {
    while (std::getline(trace, line)) {
      ++line_number;
      if (line.empty() || line.front() == '#') {
        continue;
      }
      const auto fields = split_csv(line);
      if (fields.size() < 4 || fields.size() > 5) {
        throw std::runtime_error("expected 4 or 5 CSV columns");
      }
      const auto op = fields[1] == "READ" || fields[1] == "read"
                          ? hbmsim::HbmOp::Read
                          : fields[1] == "WRITE" || fields[1] == "write"
                                ? hbmsim::HbmOp::Write
                                : throw std::runtime_error("op must be READ or WRITE");
      hbmsim::HbmTransaction transaction{
          id++, op, parse_u64(fields[2]), parse_u64(fields[3]), parse_u64(fields[0]),
          fields.size() == 5 ? static_cast<hbmsim::ClientId>(parse_u64(fields[4])) : 0};
      const auto result = system.submit(transaction);
      if (!result.accepted()) {
        throw std::runtime_error(result.message);
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "trace line " << line_number << ": " << error.what() << '\n';
    return 2;
  }

  system.run();
  const auto& stats = system.stats();
  std::cout << "completed_transactions," << stats.completed_transactions << '\n'
            << "modeled_accesses," << stats.modeled_accesses << '\n'
            << "issued_commands," << stats.issued_commands << '\n'
            << "read_bytes," << stats.read_bytes << '\n'
            << "write_bytes," << stats.write_bytes << '\n'
            << "row_hits," << stats.row_hits << '\n'
            << "row_closed," << stats.row_closed << '\n'
            << "row_conflicts," << stats.row_conflicts << '\n';
  for (std::size_t channel = 0; channel < stats.channels.size(); ++channel) {
    std::cout << "channel_" << channel << "_bytes,"
              << stats.channels[channel].completed_bytes << '\n'
              << "channel_" << channel << "_data_bus_busy_ps,"
              << stats.channels[channel].data_bus_busy_time << '\n'
              << "channel_" << channel << "_refreshes,"
              << stats.channels[channel].refreshes << '\n';
  }
  return 0;
}
