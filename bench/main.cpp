//
// plume bench
//

#include "bench.h"
#include "bench_allocations.h"
#include "bench_encoding.h"
#include "bench_descriptors.h"
#include "perf_workloads.h"

#include "plume_render_interface.h"

#ifdef __APPLE__
#include <pthread/qos.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace plume {
#ifdef __APPLE__
    extern std::unique_ptr<RenderInterface> CreateMetalInterface();
#endif
    extern std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

struct BenchOptions {
    std::string suiteName;
    std::string jsonOutput;
    std::string compareFile;
    std::string compareBinary;
    uint32_t rounds = 3;
    uint32_t iterationOverride = 0;
    bool quiet = false;
    bool gate = false;
};

BenchOptions parseArgs(int argc, char *argv[]) {
    BenchOptions opts;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
            opts.suiteName = argv[++i];
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            opts.jsonOutput = argv[++i];
        } else if (strcmp(argv[i], "--compare") == 0 && i + 1 < argc) {
            opts.compareFile = argv[++i];
        } else if (strcmp(argv[i], "--compare-binary") == 0 && i + 1 < argc) {
            opts.compareBinary = argv[++i];
        } else if (strcmp(argv[i], "--rounds") == 0 && i + 1 < argc) {
            opts.rounds = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
            opts.iterationOverride = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--quiet") == 0) {
            opts.quiet = true;
        } else if (strcmp(argv[i], "--gate") == 0) {
            opts.gate = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: plume_bench [--suite name] [--json file] [--compare file] [--iterations n] [--quiet] [--gate]\n");
            printf("                   [--compare-binary file] [--rounds n]\n");
            printf("\nSuites: allocations, encoding, descriptors, workloads\n");
            printf("\n--compare-binary runs this binary and the given baseline binary in alternating\n");
            printf("rounds (interleaved A/B), which cancels machine-state drift; prefer it over\n");
            printf("--compare for gating. --gate exits 2 on a significant regression.\n");
            exit(0);
        }
    }
    return opts;
}

// Interleaved A/B comparison: alternate fresh subprocess runs of the baseline binary
// and this binary. Both sides of each round share machine state (thermal, caches,
// background load), so per-round deltas cancel the cross-run drift that makes a
// single-shot --compare unreliable for gating.
int runDuel(const char *selfPath, const BenchOptions &opts) {
    auto runOne = [&](const std::string &binary, const std::string &jsonPath) -> bool {
        std::string cmd = "\"" + binary + "\" --quiet --json \"" + jsonPath + "\"";
        if (!opts.suiteName.empty()) {
            cmd += " --suite " + opts.suiteName;
        }
        if (opts.iterationOverride > 0) {
            cmd += " --iterations " + std::to_string(opts.iterationOverride);
        }
#ifdef _WIN32
        cmd = "\"" + cmd + "\""; // cmd.exe /C strips one layer of quotes
#endif
        return std::system(cmd.c_str()) == 0;
    };

    const uint32_t rounds = (opts.rounds > 0) ? opts.rounds : 1;

    // Discarded warm-up round: the session's first subprocess pays cold-start costs
    // (dyld/shader caches, GPU init) that run 15%+ slow and would bias round 0.
    if (!opts.quiet) {
        printf("Duel warm-up round...\n");
    }
    {
        const std::string warmupJson = "plume_duel_warmup.json";
        if (!runOne(opts.compareBinary, warmupJson) || !runOne(selfPath, warmupJson)) {
            fprintf(stderr, "Error: duel subprocess failed\n");
            return 1;
        }
        remove(warmupJson.c_str());
    }

    std::vector<std::vector<plume::bench::BenchResult>> baseRounds, curRounds;
    for (uint32_t r = 0; r < rounds; r++) {
        if (!opts.quiet) {
            printf("Duel round %u/%u...\n", r + 1, rounds);
        }
        const std::string baseJson = "plume_duel_base_" + std::to_string(r) + ".json";
        const std::string curJson = "plume_duel_cur_" + std::to_string(r) + ".json";
        if (!runOne(opts.compareBinary, baseJson) || !runOne(selfPath, curJson)) {
            fprintf(stderr, "Error: duel subprocess failed\n");
            return 1;
        }

        baseRounds.push_back(plume::bench::loadBaselineJson(baseJson));
        curRounds.push_back(plume::bench::loadBaselineJson(curJson));
        remove(baseJson.c_str());
        remove(curJson.c_str());
        if (baseRounds.back().empty() || curRounds.back().empty()) {
            fprintf(stderr, "Error: duel subprocess produced no results\n");
            return 1;
        }
    }

    auto rows = plume::bench::compareDuelRounds(baseRounds, curRounds);
    printf("\nInterleaved comparison vs %s (%u rounds):\n", opts.compareBinary.c_str(), rounds);
    printf("%-40s %12s %12s %10s %-10s\n", "Benchmark", "Baseline", "Current", "Delta", "Verdict");
    printf("%-40s %12s %12s %10s %-10s\n",
           "----------------------------------------",
           "------------", "------------", "----------", "----------");
    bool regressed = false;
    for (const auto &row : rows) {
        const char *verdict = (row.verdict == plume::bench::Significance::Improved) ? "improved"
                            : (row.verdict == plume::bench::Significance::Regressed) ? "regressed" : "noise";
        regressed = regressed || (row.verdict == plume::bench::Significance::Regressed);
        printf("%-40s %10.2f%s %10.2f%s %+9.1f%% %-10s\n",
               row.name.c_str(),
               row.medianBaseline, row.unit.c_str(),
               row.medianCurrent, row.unit.c_str(),
               row.medianDeltaPct, verdict);
    }
    printf("\n");

    if (opts.gate && regressed) {
        fprintf(stderr, "Gate: significant regression detected\n");
        return 2;
    }
    return 0;
}

int main(int argc, char *argv[]) {
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    BenchOptions opts = parseArgs(argc, argv);

    if (!opts.compareBinary.empty()) {
        return runDuel(argv[0], opts);
    }

    if (!opts.quiet) {
        printf("plume bench\n");
        printf("===========\n\n");
    }

#ifdef __APPLE__
    auto renderInterface = plume::CreateMetalInterface();
#else
    auto renderInterface = plume::CreateVulkanInterface();
#endif

    if (!renderInterface) {
        fprintf(stderr, "Error: Failed to create render interface\n");
        return 1;
    }

    auto device = renderInterface->createDevice();
    if (!device) {
        fprintf(stderr, "Error: Failed to create render device\n");
        return 1;
    }

    auto commandQueue = device->createCommandQueue(plume::RenderCommandListType::DIRECT);
    auto shaderFormat = renderInterface->getCapabilities().shaderFormat;

    if (!opts.quiet) {
        const auto &desc = device->getDescription();
        printf("Device: %s\n", desc.name.c_str());
        printf("API: %s\n\n", renderInterface->getCapabilities().shaderFormat == plume::RenderShaderFormat::METAL ? "Metal" : "Vulkan");
    }

    std::vector<plume::bench::BenchResult> allResults;
    std::string allJson = "[\n";
    bool firstSuite = true;

    auto runSuite = [&](plume::bench::BenchSuite suite) {
        if (!opts.suiteName.empty() && opts.suiteName != suite.name) {
            return;
        }

        if (opts.iterationOverride > 0) {
            for (auto &c : suite.cases) {
                c.config.measuredIterations = opts.iterationOverride;
            }
        }

        auto results = plume::bench::runSuite(suite);

        if (!opts.quiet) {
            plume::bench::printResults(suite.name, results);
        }

        for (auto &r : results) {
            allResults.push_back(r);
        }

        if (!opts.jsonOutput.empty()) {
            if (!firstSuite) allJson += ",\n";
            allJson += plume::bench::resultsToJson(suite.name, results);
            firstSuite = false;
        }
    };

    runSuite(plume::bench::createAllocationSuite(device.get()));
    runSuite(plume::bench::createDescriptorSuite(device.get()));
    runSuite(plume::bench::createEncodingSuite(device.get(), commandQueue.get(), shaderFormat));

    {
        plume::bench::WorkloadPerfConfig wpConfig;
        if (opts.iterationOverride > 0) {
            wpConfig.measuredIterations = opts.iterationOverride;
        }
        if (opts.suiteName.empty() || opts.suiteName == "workloads") {
            auto wlResults = plume::bench::runWorkloadPerfSuite(device.get(), commandQueue.get(), shaderFormat, wpConfig);
            if (!opts.quiet) {
                plume::bench::printResults("workloads", wlResults);
            }
            for (auto &r : wlResults) {
                allResults.push_back(r);
            }
            if (!opts.jsonOutput.empty()) {
                if (!firstSuite) allJson += ",\n";
                allJson += plume::bench::resultsToJson("workloads", wlResults);
                firstSuite = false;
            }
        }
    }

    if (!opts.jsonOutput.empty()) {
        allJson += "]\n";
        std::ofstream out(opts.jsonOutput);
        if (out.is_open()) {
            out << allJson;
            if (!opts.quiet) {
                printf("Results written to: %s\n", opts.jsonOutput.c_str());
            }
        } else {
            fprintf(stderr, "Error: could not write to '%s'\n", opts.jsonOutput.c_str());
        }
    }

    if (!opts.compareFile.empty()) {
        auto baseline = plume::bench::loadBaselineJson(opts.compareFile);
        if (!baseline.empty()) {
            printf("\nComparison vs baseline (%s):\n", opts.compareFile.c_str());
            plume::bench::printComparison(baseline, allResults);
            if (opts.gate && plume::bench::hasRegression(baseline, allResults)) {
                fprintf(stderr, "Gate: significant regression detected\n");
                return 2;
            }
        }
    }

    return 0;
}
