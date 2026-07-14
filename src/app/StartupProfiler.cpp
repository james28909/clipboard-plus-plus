#include "StartupProfiler.h"

#include <fstream>
#include <iomanip>

namespace {

double Milliseconds(StartupProfiler::Clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

} // namespace

StartupProfiler::StartupProfiler() : m_started(Clock::now()) {}

StartupProfiler::TimePoint StartupProfiler::BeginStage() const {
    return Clock::now();
}

void StartupProfiler::FinishStage(const std::string& name, TimePoint started) {
    const TimePoint now = Clock::now();
    m_timings.push_back({name, Milliseconds(now - started),
                         Milliseconds(now - m_started)});
}

void StartupProfiler::RecordDuration(const std::string& name, double durationMs) {
    m_timings.push_back({name, durationMs, ElapsedMs()});
}

void StartupProfiler::RecordMetric(const std::string& name,
                                   const std::string& value) {
    for (StartupMetric& metric : m_metrics) {
        if (metric.name == name) {
            metric.value = value;
            return;
        }
    }
    m_metrics.push_back({name, value});
}

double StartupProfiler::ElapsedMs() const {
    return Milliseconds(Clock::now() - m_started);
}

bool StartupProfiler::WriteReport(const std::filesystem::path& path) const {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output << "Clipboard++ startup profile\n"
           << "stage\tduration_ms\tcompleted_at_ms\n";
    output << std::fixed << std::setprecision(3);
    for (const StartupTiming& timing : m_timings)
        output << timing.name << '\t' << timing.durationMs << '\t'
               << timing.completedAtMs << '\n';
    output << "\nmetric\tvalue\n";
    for (const StartupMetric& metric : m_metrics)
        output << metric.name << '\t' << metric.value << '\n';
    return output.good();
}
