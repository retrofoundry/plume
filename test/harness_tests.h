//
// plume bench
//
// CPU-only unit tests for the bench comparison/gate logic itself. These run as
// verify-only workloads (no GPU) so a broken gate fails the correctness suite.

#pragma once

#include "bench.h"
#include "workload.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace plume::bench {

    inline BenchResult makeHarnessResult(const char *name, double value, double cov) {
        BenchResult r;
        r.name = name;
        r.unit = "us";
        r.iterations = 50;
        r.median = value;
        r.mean = value;
        r.stddev = value * cov;
        r.p5 = value * (1.0 - 2.0 * cov);
        r.p95 = value * (1.0 + 2.0 * cov);
        r.min = r.p5;
        r.max = r.p95;
        r.cov = cov;
        r.cpuEncode = value;
        r.gpuComplete = value;
        return r;
    }

    inline WorkloadSuite createHarnessSuite() {
        WorkloadSuite s;
        s.name = "harness";

        // Results must pair by NAME: a baseline from an older binary with a different
        // benchmark list must not silently misalign the comparison.
        {
            Workload w;
            w.name = "compare_matches_by_name";
            w.verify = []() -> VerifyResult {
                std::vector<BenchResult> baseline = {
                    makeHarnessResult("alpha", 100.0, 0.01),
                    makeHarnessResult("beta", 10.0, 0.01),
                };
                std::vector<BenchResult> current = {
                    makeHarnessResult("beta", 10.0, 0.01),    // reordered
                    makeHarnessResult("gamma", 50.0, 0.01),   // new bench, no baseline
                    makeHarnessResult("alpha", 100.0, 0.01),
                };
                auto rows = compareResults(baseline, current);
                if (rows.size() != 2) {
                    return VerifyResult::fail("expected 2 matched rows, got " + std::to_string(rows.size()));
                }
                for (const auto &row : rows) {
                    if (row.deltaPct != 0.0) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "row '%s' paired wrong values: delta %.1f%%, expected 0%%",
                                 row.name.c_str(), row.deltaPct);
                        return VerifyResult::fail(buf);
                    }
                    if (row.verdict != Significance::Noise) {
                        return VerifyResult::fail("identical values must classify as Noise: " + row.name);
                    }
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // The observed A/A false gate: +4.8% delta with tight CoVs must be Noise.
        // The band floor is 5% because cross-run drift dwarfs within-run CoV.
        {
            Workload w;
            w.name = "noise_band_floor_absorbs_small_drift";
            w.verify = []() -> VerifyResult {
                double band = noiseBandPct(0.01, 0.01);
                if (band < 5.0) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "band %.2f%% below the 5%% floor", band);
                    return VerifyResult::fail(buf);
                }
                if (classifyDelta(4.8, band) != Significance::Noise) {
                    return VerifyResult::fail("+4.8% with tight CoVs classified as signal");
                }
                if (classifyDelta(-4.8, band) != Significance::Noise) {
                    return VerifyResult::fail("-4.8% with tight CoVs classified as signal");
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // Noisy runs widen the band: the baseline's CoV pools with the current run's.
        {
            Workload w;
            w.name = "noise_band_pools_baseline_and_current_cov";
            w.verify = []() -> VerifyResult {
                double band = noiseBandPct(0.04, 0.03); // pooled sigma 5% -> band 10%
                if (band < 9.9 || band > 10.1) {
                    char buf[96];
                    snprintf(buf, sizeof(buf), "pooled band %.2f%%, expected ~10%%", band);
                    return VerifyResult::fail(buf);
                }
                if (classifyDelta(8.0, band) != Significance::Noise) {
                    return VerifyResult::fail("+8% inside a 10% band classified as signal");
                }
                if (classifyDelta(12.0, band) != Significance::Regressed) {
                    return VerifyResult::fail("+12% outside a 10% band not classified as regression");
                }
                if (classifyDelta(-12.0, band) != Significance::Improved) {
                    return VerifyResult::fail("-12% outside a 10% band not classified as improvement");
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // hasRegression must respect name matching and the pooled band.
        {
            Workload w;
            w.name = "gate_flags_only_real_regressions";
            w.verify = []() -> VerifyResult {
                std::vector<BenchResult> baseline = {
                    makeHarnessResult("alpha", 100.0, 0.01),
                    makeHarnessResult("beta", 10.0, 0.01),
                };
                std::vector<BenchResult> drifted = {
                    makeHarnessResult("beta", 10.3, 0.01),    // +3%: noise
                    makeHarnessResult("alpha", 104.0, 0.01),  // +4%: noise
                };
                if (hasRegression(baseline, drifted)) {
                    return VerifyResult::fail("sub-band drift flagged as regression");
                }
                std::vector<BenchResult> regressed = {
                    makeHarnessResult("beta", 10.0, 0.01),
                    makeHarnessResult("alpha", 120.0, 0.01),  // +20%: real
                };
                if (!hasRegression(baseline, regressed)) {
                    return VerifyResult::fail("+20% regression not flagged");
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // The baseline JSON must round-trip everything the comparison needs — including
        // cov, which the band calculation pools.
        {
            Workload w;
            w.name = "baseline_json_round_trip";
            w.verify = []() -> VerifyResult {
                std::vector<BenchResult> results = {
                    makeHarnessResult("alpha", 123.5, 0.04),
                    makeHarnessResult("beta", 6.25, 0.12),
                };
                std::string json = resultsToJson("harness", results);
                std::string path = "plume_harness_roundtrip.json";
                FILE *f = fopen(path.c_str(), "w");
                if (f == nullptr) {
                    return VerifyResult::fail("could not write temp json");
                }
                fwrite(json.data(), 1, json.size(), f);
                fclose(f);

                auto loaded = loadBaselineJson(path);
                remove(path.c_str());
                if (loaded.size() != 2) {
                    return VerifyResult::fail("expected 2 results, got " + std::to_string(loaded.size()));
                }
                for (size_t i = 0; i < 2; i++) {
                    const BenchResult &in = results[i], &out = loaded[i];
                    auto close = [](double a, double b) { return std::abs(a - b) < 1e-4; };
                    if (out.name != in.name || !close(out.cpuEncode, in.cpuEncode) ||
                        !close(out.median, in.median) || !close(out.cov, in.cov)) {
                        return VerifyResult::fail("round-trip mismatch on " + in.name);
                    }
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // Interleaved (duel) verdicts: a delta only counts when its sign persists
        // across every round AND the median exceeds the floor. Drift that flips sign
        // or stays small is noise.
        {
            Workload w;
            w.name = "duel_verdict_requires_persistent_sign_and_floor";
            w.verify = []() -> VerifyResult {
                if (judgeDuelDeltas({6.0, 7.0, 8.0}) != Significance::Regressed) {
                    return VerifyResult::fail("persistent +6..8% not flagged as regression");
                }
                if (judgeDuelDeltas({6.0, -2.0, 8.0}) != Significance::Noise) {
                    return VerifyResult::fail("sign-flipping deltas not treated as noise");
                }
                if (judgeDuelDeltas({3.0, 3.0, 3.0}) != Significance::Noise) {
                    return VerifyResult::fail("persistent but sub-floor +3% not treated as noise");
                }
                if (judgeDuelDeltas({-6.0, -7.0, -9.0}) != Significance::Improved) {
                    return VerifyResult::fail("persistent -6..9% not flagged as improvement");
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        // Duel rows pair by name within each round and take the median per-round delta.
        {
            Workload w;
            w.name = "duel_rows_pair_rounds_by_name";
            w.verify = []() -> VerifyResult {
                // Three rounds; "alpha" regresses 10% consistently, "beta" flips sign.
                std::vector<std::vector<BenchResult>> base = {
                    {makeHarnessResult("beta", 10.0, 0.01), makeHarnessResult("alpha", 100.0, 0.01)},
                    {makeHarnessResult("alpha", 100.0, 0.01), makeHarnessResult("beta", 10.0, 0.01)},
                    {makeHarnessResult("alpha", 100.0, 0.01), makeHarnessResult("beta", 10.0, 0.01)},
                };
                std::vector<std::vector<BenchResult>> cur = {
                    {makeHarnessResult("alpha", 110.0, 0.01), makeHarnessResult("beta", 11.0, 0.01)},
                    {makeHarnessResult("alpha", 110.0, 0.01), makeHarnessResult("beta", 9.0, 0.01)},
                    {makeHarnessResult("alpha", 110.0, 0.01), makeHarnessResult("beta", 10.0, 0.01)},
                };
                auto rows = compareDuelRounds(base, cur);
                if (rows.size() != 2) {
                    return VerifyResult::fail("expected 2 duel rows, got " + std::to_string(rows.size()));
                }
                for (const auto &row : rows) {
                    if (row.name == "alpha") {
                        if (row.verdict != Significance::Regressed) {
                            return VerifyResult::fail("alpha: persistent +10% not flagged");
                        }
                        if (std::abs(row.medianDeltaPct - 10.0) > 0.1) {
                            char buf[96];
                            snprintf(buf, sizeof(buf), "alpha: median delta %.2f%%, expected 10%%", row.medianDeltaPct);
                            return VerifyResult::fail(buf);
                        }
                    } else if (row.name == "beta") {
                        if (row.verdict != Significance::Noise) {
                            return VerifyResult::fail("beta: sign-flipping drift not treated as noise");
                        }
                    } else {
                        return VerifyResult::fail("unexpected row " + row.name);
                    }
                }
                return VerifyResult::ok();
            };
            s.workloads.push_back(w);
        }

        return s;
    }

}
