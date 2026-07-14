#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

struct StartupTiming {
    std::string name;
    double durationMs{};
    double completedAtMs{};
};

struct StartupMetric {
    std::string name;
    std::string value;
};

class StartupProfiler {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    StartupProfiler();

    TimePoint BeginStage() const;
    void FinishStage(const std::string& name, TimePoint started);
    void RecordDuration(const std::string& name, double durationMs);
    void RecordMetric(const std::string& name, const std::string& value);
    double ElapsedMs() const;
    const std::vector<StartupTiming>& Timings() const { return m_timings; }
    const std::vector<StartupMetric>& Metrics() const { return m_metrics; }
    bool WriteReport(const std::filesystem::path& path) const;

private:
    TimePoint m_started;
    std::vector<StartupTiming> m_timings;
    std::vector<StartupMetric> m_metrics;
};
