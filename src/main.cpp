#include "hbmsim/hbm_system.h"

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
  if (argc < 2) {
    std::cerr << "usage: hbmsim TRACE.csv [--profile hbm2_2000|hbm3_6400] [--completions FILE.csv]\n"
                 "columns: arrival_ps,op,address,size_bytes[,client]\n";
    return 2;
  }
  std::string completion_path;
  hbmsim::HbmConfig config;
  for (int index = 2; index < argc; index += 2) {
    if (index + 1 >= argc) {
      std::cerr << "option " << argv[index] << " requires a value\n";
      return 2;
    }
    const std::string option(argv[index]);
    const std::string value(argv[index + 1]);
    if (option == "--completions") {
      completion_path = value;
    } else if (option == "--profile") {
      if (value == "hbm2_2000") {
        config = hbmsim::HbmConfig::hbm2_2000();
      } else if (value == "hbm3_6400") {
        config = hbmsim::HbmConfig::hbm3_6400();
      } else {
        std::cerr << "unknown profile: " << value << '\n';
        return 2;
      }
    } else {
      std::cerr << "unknown option: " << option << '\n';
      return 2;
    }
  }

  std::ifstream trace(argv[1]);
  if (!trace) {
    std::cerr << "cannot open trace: " << argv[1] << '\n';
    return 2;
  }

  hbmsim::HbmSystem system(config);
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
  if (!completion_path.empty()) {
    std::ofstream completions(completion_path);
    if (!completions) {
      std::cerr << "cannot write completions: " << argv[3] << '\n';
      return 2;
    }
    completions << "id,op,address,size_bytes,channel,arrival_ps,completion_ps,latency_ps,access_class\n";
    for (const auto& completion : system.completions()) {
      const char* op = completion.op == hbmsim::HbmOp::Read ? "READ" : "WRITE";
      const char* access_class = completion.access_class == hbmsim::HbmAccessClass::RowHit
                                     ? "row_hit"
                                     : completion.access_class == hbmsim::HbmAccessClass::RowClosed
                                           ? "row_closed"
                                           : "row_conflict";
      completions << completion.id << ',' << op << ',' << completion.address << ','
                  << completion.size_bytes << ',' << completion.channel << ','
                  << completion.arrival_time << ',' << completion.completion_time << ','
                  << completion.latency << ',' << access_class << '\n';
    }
  }
  const auto& stats = system.stats();
  std::cout << "completed_transactions," << stats.completed_transactions << '\n'
            << "modeled_accesses," << stats.modeled_accesses << '\n'
            << "issued_commands," << stats.issued_commands << '\n'
            << "read_bytes," << stats.read_bytes << '\n'
            << "write_bytes," << stats.write_bytes << '\n'
            << "row_hits," << stats.row_hits << '\n'
            << "row_closed," << stats.row_closed << '\n'
            << "row_conflicts," << stats.row_conflicts << '\n'
            << "act_commands," << stats.act_commands << '\n'
            << "pre_commands," << stats.pre_commands << '\n'
            << "read_commands," << stats.read_commands << '\n'
            << "write_commands," << stats.write_commands << '\n'
            << "rfm_events," << stats.rfm_events << '\n';
  for (std::size_t channel = 0; channel < stats.channels.size(); ++channel) {
    std::cout << "channel_" << channel << "_bytes,"
              << stats.channels[channel].completed_bytes << '\n'
              << "channel_" << channel << "_data_bus_busy_ps,"
              << stats.channels[channel].data_bus_busy_time << '\n'
              << "channel_" << channel << "_refreshes,"
              << stats.channels[channel].refreshes << '\n'
              << "channel_" << channel << "_per_bank_refreshes,"
              << stats.channels[channel].per_bank_refreshes << '\n'
              << "channel_" << channel << "_rfm_events,"
              << stats.channels[channel].rfm_events << '\n';
  }
  return 0;
}
