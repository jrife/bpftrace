#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "types.h"

namespace bpftrace::util {

template <typename T>
std::pair<T, uint64_t> reduce_tseries_value(const std::vector<uint8_t> &values,
                                            int nvalues,
                                            const SizedType &inner_type)
{
  // Combine values only from the same epoch and return the reduced value from
  // the latest epoch in this bucket.
  std::map<uint64_t, std::pair<T, uint64_t>> epoch_to_value;
  uint64_t latest_epoch = 0;

  for (int i = 0; i < nvalues; i++) {
    const uint8_t *val = values.data() + i * sizeof(uint64_t) * 3;
    uint64_t meta = read_data<uint64_t>(val + sizeof(uint64_t));
    uint64_t epoch = read_data<uint64_t>(val + sizeof(uint64_t) * 2);

    if (epoch == 0) {
      // Don't consider buckets where epoch is 0. This means it was never used.
      continue;
    }

    if (epoch > latest_epoch) {
      latest_epoch = epoch;
    }

    if (epoch_to_value.find(epoch) == epoch_to_value.end()) {
      epoch_to_value[epoch] = std::pair<T, uint64_t>(read_data<T>(val), meta);

      continue;
    }

    std::pair<T, uint64_t> &current = epoch_to_value[epoch];

    if (inner_type.IsIntegerTy()) {
      // If the inner type is simply an integer, the most recently assigned
      // value wins. Use the timestamp to decide which is the most recent
      // value.
      if (meta > current.second) {
        current.first = read_data<T>(val);
        current.second = meta;
      }
    } else if (inner_type.IsCountTy() || inner_type.IsSumTy()) {
      current.first += read_data<T>(val);
    } else if (inner_type.IsMaxTy() || inner_type.IsMinTy()) {
      T mm_val = read_data<T>(val);

      if ((inner_type.IsMaxTy() && mm_val > current.first) ||
          (inner_type.IsMinTy() && mm_val < current.first)) {
        current.first = mm_val;
      }
    } else if (inner_type.IsAvgTy()) {
      T sum_val = read_data<T>(val);

      current.first += sum_val;
      current.second += meta;
    }
  }

  if (latest_epoch == 0) {
    return std::pair<T, uint64_t>(0, 0);
  }

  std::pair<T, uint64_t> &latest = epoch_to_value[latest_epoch];
  if (inner_type.IsAvgTy()) {
    latest.first = (T)(latest.first / latest.second);
  }

  latest.second = latest_epoch;

  return latest;
}

} // namespace bpftrace::util
