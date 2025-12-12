/**
 * @file exhaustive_runner.h
 * @brief Exhaustive test runner - executes tests across modes/channels
 */

#ifndef EXHAUSTIVE_RUNNER_H
#define EXHAUSTIVE_RUNNER_H

#include "cli.h"
#include "output.h"
#include "../test_framework.h"

#include <chrono>
#include <memory>

namespace exhaustive {

using namespace test_framework;
using namespace std::chrono;

// ============================================================
// Exhaustive Test Runner
// ============================================================

class ExhaustiveRunner {
public:
    ExhaustiveRunner(ITestBackend& backend, IOutput& output, const Config& cfg)
        : backend_(backend), output_(output), cfg_(cfg) {}
    
    TestResults run() {
        TestResults results;
        
        // Get modes and channels
        auto all_modes = get_all_modes();
        auto modes = filter_modes(all_modes, cfg_.mode_filter);
        auto channels = get_standard_channels();
        
        if (modes.empty()) {
            output_.on_error("No modes match filter: " + cfg_.mode_filter);
            return results;
        }
        
        // Prepare test data
        std::vector<uint8_t> test_data(cfg_.test_message.begin(), cfg_.test_message.end());
        
        // Calculate iterations based on duration or explicit count
        int max_iterations = cfg_.max_iterations;
        bool use_duration = cfg_.duration_seconds > 0;
        
        auto start_time = steady_clock::now();
        steady_clock::time_point end_time;
        if (use_duration) {
            end_time = start_time + seconds(cfg_.duration_seconds);
            max_iterations = 999999;  // Effectively unlimited
        }
        
        // Output start info
        output_.on_start(
            backend_.backend_name(),
            cfg_.use_auto_detect ? "AUTO" : "KNOWN (AFC-friendly)",
            cfg_.equalizers,
            use_duration ? cfg_.duration_seconds : cfg_.max_iterations,
            use_duration,
            cfg_.mode_filter
        );
        
        // Build job list
        struct TestJob {
            std::string eq;
            ModeInfo mode;
            ChannelCondition channel;
            std::string record_name;
        };
        
        int iteration = 0;
        bool should_stop = false;
        
        while (!should_stop) {
            iteration++;
            
            // Check termination
            if (use_duration) {
                if (steady_clock::now() >= end_time) break;
            } else {
                if (iteration > max_iterations) break;
            }
            
            for (const auto& eq : cfg_.equalizers) {
                backend_.set_equalizer(eq);
                
                for (const auto& mode : modes) {
                    for (const auto& channel : channels) {
                        // Check time again for duration mode
                        if (use_duration && steady_clock::now() >= end_time) {
                            should_stop = true;
                            break;
                        }
                        
                        auto now = steady_clock::now();
                        int elapsed = (int)duration_cast<seconds>(now - start_time).count();
                        int remaining = use_duration ? 
                            (int)duration_cast<seconds>(end_time - now).count() : 0;
                        
                        // Mode name (include eq if multiple)
                        std::string mode_name = (cfg_.equalizers.size() > 1) ? 
                            eq + ":" + mode.name : mode.name;
                        
                        // Run test
                        double ber;
                        bool passed = backend_.run_test(mode, channel, test_data, ber);
                        
                        // Record result
                        results.record(mode_name, channel.name, passed, ber);
                        
                        // Output result
                        output_.on_test_result(
                            elapsed,
                            mode_name,
                            channel.name,
                            results.total_tests,
                            results.total_passed(),
                            results.overall_pass_rate(),
                            passed,
                            ber,
                            iteration,
                            max_iterations
                        );
                    }
                    if (should_stop) break;
                }
                if (should_stop) break;
            }
        }
        
        // Final timing
        auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
        results.iterations = iteration - 1;
        results.duration_seconds = (int)total_elapsed;
        
        // Output mode stats
        if (!cfg_.json_output) {
            output_.on_info("\n--- BY MODE ---");
            output_.on_info("Mode        Passed  Failed   Total     Rate      Avg BER");
            output_.on_info("----------------------------------------------------------");
        }
        for (const auto& [mode, stats] : results.mode_stats) {
            output_.on_mode_stats(mode, stats.passed, stats.failed, stats.total,
                                 stats.pass_rate(), stats.avg_ber());
        }
        
        // Output channel stats
        if (!cfg_.json_output) {
            output_.on_info("\n--- BY CHANNEL ---");
            output_.on_info("Channel             Passed  Failed   Total     Rate      Avg BER");
            output_.on_info("------------------------------------------------------------------");
        }
        for (const auto& [channel, stats] : results.channel_stats) {
            output_.on_channel_stats(channel, stats.passed, stats.failed, stats.total,
                                    stats.pass_rate(), stats.avg_ber());
        }
        
        // Overall summary
        double overall_ber = 0.0;
        int ber_count = 0;
        for (const auto& [_, stats] : results.mode_stats) {
            if (stats.ber_tests > 0) {
                overall_ber += stats.total_ber;
                ber_count += stats.ber_tests;
            }
        }
        if (ber_count > 0) overall_ber /= ber_count;
        
        output_.on_done(
            results.duration_seconds,
            results.iterations,
            results.total_tests,
            results.total_passed(),
            results.total_failed(),
            results.overall_pass_rate(),
            overall_ber,
            results.rating(),
            ""  // Report file set later
        );
        
        return results;
    }
    
    // Parallel execution variant
    TestResults run_parallel() {
        TestResults results;
        
        auto all_modes = get_all_modes();
        auto modes = filter_modes(all_modes, cfg_.mode_filter);
        auto channels = get_standard_channels();
        
        if (modes.empty()) {
            output_.on_error("No modes match filter");
            return results;
        }
        
        std::vector<uint8_t> test_data(cfg_.test_message.begin(), cfg_.test_message.end());
        
        // Build all jobs upfront
        struct TestJob {
            std::string eq;
            ModeInfo mode;
            ChannelCondition channel;
            std::string record_name;
        };
        
        std::vector<TestJob> all_jobs;
        for (int iter = 0; iter < cfg_.max_iterations; iter++) {
            for (const auto& eq : cfg_.equalizers) {
                for (const auto& mode : modes) {
                    for (const auto& channel : channels) {
                        TestJob job;
                        job.eq = eq;
                        job.mode = mode;
                        job.channel = channel;
                        job.record_name = (cfg_.equalizers.size() > 1) ? 
                            eq + ":" + mode.name : mode.name;
                        all_jobs.push_back(job);
                    }
                }
            }
        }
        
        output_.on_start(
            backend_.backend_name() + " (parallel)",
            cfg_.use_auto_detect ? "AUTO" : "KNOWN",
            cfg_.equalizers,
            cfg_.max_iterations,
            false,
            cfg_.mode_filter
        );
        
        output_.on_info("Running " + std::to_string(all_jobs.size()) + 
                       " tests with " + std::to_string(cfg_.parallel_threads) + " threads...");
        
        auto start_time = steady_clock::now();
        
        // Create thread pool and worker backends
        ThreadPool pool(cfg_.parallel_threads);
        
        std::vector<std::unique_ptr<ITestBackend>> worker_backends;
        for (int i = 0; i < cfg_.parallel_threads; i++) {
            auto clone = backend_.clone();
            if (clone) {
                worker_backends.push_back(std::move(clone));
            }
        }
        
        if (worker_backends.empty()) {
            output_.on_error("Backend does not support parallel execution");
            return results;
        }
        
        std::atomic<int> next_worker{0};
        std::atomic<int> completed{0};
        std::atomic<int> passed_count{0};
        int total_jobs = (int)all_jobs.size();
        
        // Enqueue all jobs
        for (size_t i = 0; i < all_jobs.size(); i++) {
            pool.enqueue([&, i]() {
                const auto& job = all_jobs[i];
                
                int worker_id = next_worker++ % cfg_.parallel_threads;
                auto* worker = worker_backends[worker_id].get();
                
                worker->set_equalizer(job.eq);
                
                double ber;
                bool pass = worker->run_test(job.mode, job.channel, test_data, ber);
                
                results.record(job.record_name, job.channel.name, pass, ber);
                completed++;
                if (pass) passed_count++;
                
                // Progress every 10 tests
                if (completed % 10 == 0) {
                    int elapsed = (int)duration_cast<seconds>(
                        steady_clock::now() - start_time).count();
                    output_.on_progress(elapsed, completed, passed_count,
                        completed > 0 ? 100.0 * passed_count / completed : 0.0, 0);
                }
            });
        }
        
        pool.wait_all();
        
        auto total_elapsed = duration_cast<seconds>(steady_clock::now() - start_time).count();
        results.iterations = cfg_.max_iterations;
        results.duration_seconds = (int)total_elapsed;
        
        // Output stats same as sequential
        for (const auto& [mode, stats] : results.mode_stats) {
            output_.on_mode_stats(mode, stats.passed, stats.failed, stats.total,
                                 stats.pass_rate(), stats.avg_ber());
        }
        
        for (const auto& [channel, stats] : results.channel_stats) {
            output_.on_channel_stats(channel, stats.passed, stats.failed, stats.total,
                                    stats.pass_rate(), stats.avg_ber());
        }
        
        output_.on_done(
            results.duration_seconds,
            results.iterations,
            results.total_tests,
            results.total_passed(),
            results.total_failed(),
            results.overall_pass_rate(),
            0.0,  // TODO: Calculate overall BER
            results.rating(),
            ""
        );
        
        return results;
    }

private:
    ITestBackend& backend_;
    IOutput& output_;
    const Config& cfg_;
};

} // namespace exhaustive

#endif // EXHAUSTIVE_RUNNER_H
