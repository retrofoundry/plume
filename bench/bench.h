//
// plume bench
//

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

#include "workload.h"

namespace plume::bench {

    struct BenchResult {
        std::string name;
        std::string unit;
        uint32_t iterations;
        double median;
        double mean;
        double stddev;
        double p5;
        double p95;
        double min;
        double max;
        double cov;
        double cpuEncode = 0.0;   // median CPU time to encode + submit (us)
        double gpuComplete = 0.0; // median wall time including GPU completion (us)
    };

    struct BenchConfig {
        uint32_t warmupIterations = 10;
        uint32_t measuredIterations = 50;
        uint32_t innerLoopCount = 1;
    };

    using BenchFn = std::function<void()>;

    struct BenchCase {
        std::string name;
        std::string unit = "us";
        BenchConfig config;
        BenchFn fn;
    };

    struct BenchSuite {
        std::string name;
        std::vector<BenchCase> cases;
    };

    inline BenchResult runBenchCase(const BenchCase &bench) {
        for (uint32_t i = 0; i < bench.config.warmupIterations; i++) {
            bench.fn();
        }

        std::vector<double> samples;
        samples.reserve(bench.config.measuredIterations);

        for (uint32_t i = 0; i < bench.config.measuredIterations; i++) {
            auto start = std::chrono::high_resolution_clock::now();
            for (uint32_t j = 0; j < bench.config.innerLoopCount; j++) {
                bench.fn();
            }
            auto end = std::chrono::high_resolution_clock::now();

            double elapsed = std::chrono::duration<double, std::micro>(end - start).count();
            samples.push_back(elapsed / bench.config.innerLoopCount);
        }

        std::sort(samples.begin(), samples.end());

        BenchResult result;
        result.name = bench.name;
        result.unit = bench.unit;
        result.iterations = bench.config.measuredIterations;
        result.min = samples.front();
        result.max = samples.back();

        size_t n = samples.size();
        result.median = (n % 2 == 0)
            ? (samples[n/2 - 1] + samples[n/2]) / 2.0
            : samples[n/2];

        result.p5 = samples[static_cast<size_t>(n * 0.05)];
        result.p95 = samples[static_cast<size_t>(n * 0.95)];

        double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
        result.mean = sum / n;

        double sqSum = 0.0;
        for (double s : samples) {
            double diff = s - result.mean;
            sqSum += diff * diff;
        }
        result.stddev = std::sqrt(sqSum / n);
        result.cov = (result.mean > 0.0) ? (result.stddev / result.mean) : 0.0;

        return result;
    }

    struct WorkloadPerfConfig {
        uint32_t warmupIterations = 10;
        uint32_t measuredIterations = 50;
        // Encodes+submits batched per timed sample. >1 amortizes fixed per-submit jitter
        // on cpu_encode (the gated metric); gpu_complete then reports amortized throughput
        // rather than single-frame latency.
        uint32_t innerLoopCount = 1;
    };

    inline BenchResult runWorkloadPerf(RenderDevice *device, RenderCommandQueue *queue, const Workload &w, const WorkloadPerfConfig &config) {
        if (w.setup) {
            w.setup();
        }

        const uint32_t inner = config.innerLoopCount > 0 ? config.innerLoopCount : 1;
        auto fence = device->createCommandFence();

        // Batches `inner` encode+submits; only the last signals the fence, so a single
        // wait drains all of them (the queue is in-order). Command lists are kept alive
        // until after the drain so no in-flight command buffer is destroyed early.
        auto oneIteration = [&](double &cpuUs, double &wallUs) {
            std::vector<std::unique_ptr<RenderCommandList>> lists;
            lists.reserve(inner);
            auto start = std::chrono::high_resolution_clock::now();
            for (uint32_t j = 0; j < inner; j++) {
                auto cmdList = queue->createCommandList();
                cmdList->begin();
                w.encode(cmdList.get());
                cmdList->end();
                const RenderCommandList *raw = cmdList.get();
                RenderCommandFence *signal = (j + 1 == inner) ? fence.get() : nullptr;
                queue->executeCommandLists(&raw, 1, nullptr, 0, nullptr, 0, signal);
                lists.push_back(std::move(cmdList));
            }
            auto submitted = std::chrono::high_resolution_clock::now();
            queue->waitForCommandFence(fence.get());
            auto completed = std::chrono::high_resolution_clock::now();
            cpuUs = std::chrono::duration<double, std::micro>(submitted - start).count() / inner;
            wallUs = std::chrono::duration<double, std::micro>(completed - start).count() / inner;
        };

        for (uint32_t i = 0; i < config.warmupIterations; i++) {
            double a, b;
            oneIteration(a, b);
        }

        std::vector<double> cpuSamples, wallSamples;
        cpuSamples.reserve(config.measuredIterations);
        wallSamples.reserve(config.measuredIterations);
        for (uint32_t i = 0; i < config.measuredIterations; i++) {
            double cpuUs, wallUs;
            oneIteration(cpuUs, wallUs);
            cpuSamples.push_back(cpuUs);
            wallSamples.push_back(wallUs);
        }

        auto median = [](std::vector<double> &v) {
            std::sort(v.begin(), v.end());
            size_t n = v.size();
            return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
        };

        BenchResult result;
        result.name = w.name;
        result.unit = "us";
        result.iterations = config.measuredIterations;

        std::vector<double> cpuSorted = cpuSamples;
        std::sort(cpuSorted.begin(), cpuSorted.end());
        result.min = cpuSorted.front();
        result.max = cpuSorted.back();
        result.median = median(cpuSorted);
        result.p5 = cpuSorted[static_cast<size_t>(cpuSorted.size() * 0.05)];
        result.p95 = cpuSorted[static_cast<size_t>(cpuSorted.size() * 0.95)];
        double sum = std::accumulate(cpuSorted.begin(), cpuSorted.end(), 0.0);
        result.mean = sum / cpuSorted.size();
        double sq = 0.0;
        for (double s : cpuSorted) { double d = s - result.mean; sq += d * d; }
        result.stddev = std::sqrt(sq / cpuSorted.size());
        result.cov = (result.mean > 0.0) ? (result.stddev / result.mean) : 0.0;

        result.cpuEncode = result.median;
        result.gpuComplete = median(wallSamples);
        return result;
    }

    inline std::vector<BenchResult> runSuite(const BenchSuite &suite) {
        std::vector<BenchResult> results;
        results.reserve(suite.cases.size());
        for (const auto &bench : suite.cases) {
            results.push_back(runBenchCase(bench));
        }
        return results;
    }

    inline std::string resultsToJson(const std::string &suiteName, const std::vector<BenchResult> &results) {
        std::string json = "{\n";
        json += "  \"suite\": \"" + suiteName + "\",\n";
        json += "  \"results\": [\n";

        for (size_t i = 0; i < results.size(); i++) {
            const auto &r = results[i];
            json += "    {\n";
            json += "      \"name\": \"" + r.name + "\",\n";
            json += "      \"unit\": \"" + r.unit + "\",\n";
            json += "      \"iterations\": " + std::to_string(r.iterations) + ",\n";
            json += "      \"median\": " + std::to_string(r.median) + ",\n";
            json += "      \"mean\": " + std::to_string(r.mean) + ",\n";
            json += "      \"stddev\": " + std::to_string(r.stddev) + ",\n";
            json += "      \"p5\": " + std::to_string(r.p5) + ",\n";
            json += "      \"p95\": " + std::to_string(r.p95) + ",\n";
            json += "      \"min\": " + std::to_string(r.min) + ",\n";
            json += "      \"max\": " + std::to_string(r.max) + ",\n";
            json += "      \"cpu_encode\": " + std::to_string(r.cpuEncode) + ",\n";
            json += "      \"gpu_complete\": " + std::to_string(r.gpuComplete) + ",\n";
            json += "      \"cov\": " + std::to_string(r.cov) + "\n";
            json += "    }";
            if (i < results.size() - 1) json += ",";
            json += "\n";
        }

        json += "  ]\n";
        json += "}\n";
        return json;
    }

    inline void printResults(const std::string &suiteName, const std::vector<BenchResult> &results) {
        printf("\n=== %s ===\n", suiteName.c_str());
        printf("%-40s %12s %12s %8s\n", "Benchmark", "CPUEncode", "GPUComplete", "CoV");
        printf("%-40s %12s %12s %8s\n",
               "----------------------------------------", "------------", "------------", "--------");
        for (const auto &r : results) {
            double cpu = (r.cpuEncode > 0.0) ? r.cpuEncode : r.median;
            printf("%-40s %10.2f%s %10.2f%s %6.1f%%\n",
                   r.name.c_str(),
                   cpu, r.unit.c_str(),
                   r.gpuComplete, r.unit.c_str(),
                   r.cov * 100.0);
        }
        printf("\n");
    }

    // A delta is significant only if it exceeds the noise band. Below that, it is noise.
    enum class Significance { Improved, Regressed, Noise };

    inline Significance classifyDelta(double deltaPct, double bandPct) {
        if (deltaPct <= -bandPct) return Significance::Improved;
        if (deltaPct >= bandPct) return Significance::Regressed;
        return Significance::Noise;
    }

    // ~2 sigma of the pooled baseline+current variation. The 5% floor exists because
    // two non-interleaved runs differ by machine state (thermal, caches, process
    // warm-up) that within-run CoV cannot see; an A/A comparison routinely drifts
    // several percent with sub-1% CoV. For verdicts below the floor, use the
    // interleaved mode (--compare-binary), which cancels that drift.
    inline double noiseBandPct(double baselineCov, double currentCov) {
        double pooled = std::sqrt(baselineCov * baselineCov + currentCov * currentCov);
        double band = 2.0 * pooled * 100.0;
        return (band < 5.0) ? 5.0 : band;
    }

    struct ComparisonRow {
        std::string name;
        std::string unit = "us";
        double baselineValue = 0.0;
        double currentValue = 0.0;
        double deltaPct = 0.0;
        Significance verdict = Significance::Noise;
    };

    inline double comparisonValue(const BenchResult &r) {
        return (r.cpuEncode > 0.0) ? r.cpuEncode : r.median;
    }

    // Pairs results by NAME (the benchmark set may differ between baseline and
    // current binaries); benches present in only one side are skipped.
    inline std::vector<ComparisonRow> compareResults(const std::vector<BenchResult> &baseline, const std::vector<BenchResult> &current) {
        std::vector<ComparisonRow> rows;
        rows.reserve(current.size());
        for (const BenchResult &cur : current) {
            const BenchResult *base = nullptr;
            for (const BenchResult &b : baseline) {
                if (b.name == cur.name) {
                    base = &b;
                    break;
                }
            }
            if (base == nullptr || comparisonValue(*base) <= 0.0) {
                continue;
            }

            ComparisonRow row;
            row.name = cur.name;
            row.unit = cur.unit;
            row.baselineValue = comparisonValue(*base);
            row.currentValue = comparisonValue(cur);
            row.deltaPct = ((row.currentValue - row.baselineValue) / row.baselineValue) * 100.0;
            row.verdict = classifyDelta(row.deltaPct, noiseBandPct(base->cov, cur.cov));
            rows.push_back(row);
        }
        return rows;
    }

    struct DuelRow {
        std::string name;
        std::string unit = "us";
        double medianBaseline = 0.0;
        double medianCurrent = 0.0;
        double medianDeltaPct = 0.0;
        Significance verdict = Significance::Noise;
    };

    // Interleaved rounds share machine state (thermal, caches), so per-round deltas
    // cancel the drift that defeats single-shot comparison. A verdict requires the
    // delta's sign to persist across EVERY round and its median to clear the floor.
    inline Significance judgeDuelDeltas(const std::vector<double> &deltaPcts, double floorPct = 5.0) {
        if (deltaPcts.empty()) {
            return Significance::Noise;
        }

        bool allPositive = true, allNegative = true;
        for (double d : deltaPcts) {
            allPositive = allPositive && (d > 0.0);
            allNegative = allNegative && (d < 0.0);
        }

        std::vector<double> sorted = deltaPcts;
        std::sort(sorted.begin(), sorted.end());
        size_t n = sorted.size();
        double median = (n % 2 == 0) ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0 : sorted[n / 2];

        if (allPositive && median >= floorPct) return Significance::Regressed;
        if (allNegative && median <= -floorPct) return Significance::Improved;
        return Significance::Noise;
    }

    inline std::vector<DuelRow> compareDuelRounds(const std::vector<std::vector<BenchResult>> &baselineRounds, const std::vector<std::vector<BenchResult>> &currentRounds) {
        std::vector<DuelRow> rows;
        if (baselineRounds.empty() || baselineRounds.size() != currentRounds.size()) {
            return rows;
        }

        // Benches are keyed by the first current round; pair each round by name.
        for (const BenchResult &keyed : currentRounds.front()) {
            std::vector<double> deltas, baseValues, curValues;
            for (size_t round = 0; round < currentRounds.size(); round++) {
                const BenchResult *base = nullptr, *cur = nullptr;
                for (const BenchResult &b : baselineRounds[round]) {
                    if (b.name == keyed.name) { base = &b; break; }
                }
                for (const BenchResult &c : currentRounds[round]) {
                    if (c.name == keyed.name) { cur = &c; break; }
                }
                if (base == nullptr || cur == nullptr || comparisonValue(*base) <= 0.0) {
                    continue;
                }
                baseValues.push_back(comparisonValue(*base));
                curValues.push_back(comparisonValue(*cur));
                deltas.push_back(((curValues.back() - baseValues.back()) / baseValues.back()) * 100.0);
            }
            if (deltas.size() != currentRounds.size()) {
                continue; // bench missing from some round on either side
            }

            auto median = [](std::vector<double> v) {
                std::sort(v.begin(), v.end());
                size_t n = v.size();
                return (n % 2 == 0) ? (v[n / 2 - 1] + v[n / 2]) / 2.0 : v[n / 2];
            };

            DuelRow row;
            row.name = keyed.name;
            row.unit = keyed.unit;
            row.medianBaseline = median(baseValues);
            row.medianCurrent = median(curValues);
            row.medianDeltaPct = median(deltas);
            row.verdict = judgeDuelDeltas(deltas);
            rows.push_back(row);
        }
        return rows;
    }

    inline std::vector<BenchResult> loadBaselineJson(const std::string &path) {
        std::vector<BenchResult> results;
        std::ifstream file(path);
        if (!file.is_open()) {
            fprintf(stderr, "Warning: could not open baseline file '%s'\n", path.c_str());
            return results;
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());

        size_t pos = 0;
        while ((pos = content.find("\"name\":", pos)) != std::string::npos) {
            BenchResult r;

            size_t nameStart = content.find('"', pos + 7) + 1;
            size_t nameEnd = content.find('"', nameStart);
            r.name = content.substr(nameStart, nameEnd - nameStart);

            size_t unitPos = content.find("\"unit\":", pos);
            if (unitPos != std::string::npos && unitPos < pos + 500) {
                size_t unitStart = content.find('"', unitPos + 7) + 1;
                size_t unitEnd = content.find('"', unitStart);
                r.unit = content.substr(unitStart, unitEnd - unitStart);
            }

            size_t medianPos = content.find("\"median\":", pos);
            if (medianPos != std::string::npos && medianPos < pos + 500) {
                r.median = atof(content.c_str() + medianPos + 9);
            }

            size_t cpuPos = content.find("\"cpu_encode\":", pos);
            if (cpuPos != std::string::npos && cpuPos < pos + 500) {
                r.cpuEncode = atof(content.c_str() + cpuPos + 13);
            }

            size_t covPos = content.find("\"cov\":", pos);
            if (covPos != std::string::npos && covPos < pos + 500) {
                r.cov = atof(content.c_str() + covPos + 6);
            }

            results.push_back(r);
            pos = nameEnd;
        }

        return results;
    }

    inline void printComparison(const std::vector<BenchResult> &baseline, const std::vector<BenchResult> &current) {
        printf("\n%-40s %12s %12s %10s %-10s\n",
               "Benchmark", "Baseline", "Current", "Delta", "Verdict");
        printf("%-40s %12s %12s %10s %-10s\n",
               "----------------------------------------",
               "------------", "------------", "----------", "----------");

        for (const ComparisonRow &row : compareResults(baseline, current)) {
            const char *verdict = (row.verdict == Significance::Improved) ? "improved"
                                : (row.verdict == Significance::Regressed) ? "regressed" : "noise";
            printf("%-40s %10.2f%s %10.2f%s %+9.1f%% %-10s\n",
                   row.name.c_str(),
                   row.baselineValue, row.unit.c_str(),
                   row.currentValue, row.unit.c_str(),
                   row.deltaPct, verdict);
        }
        printf("\n");
    }

    // True if any current result is a significant regression vs the baseline.
    inline bool hasRegression(const std::vector<BenchResult> &baseline, const std::vector<BenchResult> &current) {
        for (const ComparisonRow &row : compareResults(baseline, current)) {
            if (row.verdict == Significance::Regressed) {
                return true;
            }
        }
        return false;
    }

}
