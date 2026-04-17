#ifndef BOUND_MEM_LATENCY_H_
#define BOUND_MEM_LATENCY_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include "log.h"


class IBoundMemLatencyEstimator {
public:
    virtual ~IBoundMemLatencyEstimator() = default;

    virtual void     reset()                       = 0;
    virtual uint32_t getMemLatency() const         = 0;
    virtual void     updateModel(uint32_t latency) = 0;
};


template <typename Strategy>
class MemLatencyEstimator : public IBoundMemLatencyEstimator, public GlobAlloc {
public:
    MemLatencyEstimator(uint32_t initial) : initial_latency(initial) {}

    void reset() override { strategy.reset(); }
    uint32_t getMemLatency() const override { return strategy.getMemLatency(initial_latency); }
    void updateModel(uint32_t latency) override { strategy.updateModel(latency); }

private:
    const uint32_t initial_latency;
    Strategy strategy;
};


struct FixedStrategy {
    void reset() {}
    uint32_t getMemLatency(uint32_t initial) const { return initial ? initial : 0; }
    void     updateModel(uint32_t) {}
};

struct LastSeenStrategy {
    void reset() {}
    uint32_t getMemLatency(uint32_t initial) const { return last ? last : initial; }
    void updateModel(uint32_t latency) { last = latency; }

private:
    uint32_t last = 0;
};

struct MaxStrategy {
    void reset() {
        max_prev_weave_lat = max_lat;
        max_lat            = 0;
    }

    uint32_t getMemLatency(uint32_t initial) const { return max_prev_weave_lat == 0 ? initial : max_prev_weave_lat; }
    void     updateModel(uint32_t latency) {
            if (max_lat < latency) max_lat = latency;
    }

private:
    uint32_t max_prev_weave_lat = 0;
    uint32_t max_lat            = 0;
};

// Adaptive average with confidence gating and rate limiting. Aggregates the
// latencies observed during the detailed phase, then blends the phase average
// into the published bound with a weight proportional to the amount of new
// evidence. This allows responsiveness even under sparse traffic while still
// damping large oscillations.
struct WindowedAverageStrategy {
    void reset() {
        if (phase_count == 0) {
            phase_min = std::numeric_limits<uint32_t>::max();
            phase_max = 0;
            return;
        }

        uint64_t sum = phase_sum;
        size_t count = phase_count;
        if (count >= TrimThreshold) {
            // sum -= phase_min;
            // sum -= phase_max;
            // count -= 2;
        }

        if (count == 0) {
            phase_count = 0;
            phase_sum = 0;
            phase_min = std::numeric_limits<uint32_t>::max();
            phase_max = 0;
            return;
        }

        const double phase_avg = static_cast<double>(sum) / static_cast<double>(count);
        const bool have_history = has_published;
        const double base = have_history ? static_cast<double>(published_latency) : phase_avg;

        double weight = have_history ? std::min(1.0, static_cast<double>(phase_count) / EffectiveWindow) : 1.0;
        if (have_history) weight = std::max(weight, MinPhaseWeight);

        double proposed = base + weight * (phase_avg - base);

        if (have_history) {
            const double lower = std::max(1.0, base * (1.0 - MaxDropRatio));
            const double upper = std::max(lower + 1.0, base * (1.0 + MaxRiseRatio));
            if (proposed < lower) proposed = lower;
            if (proposed > upper) proposed = upper;
        }

        published_latency = static_cast<uint32_t>(proposed + 0.5);
        if (published_latency == 0) published_latency = 1;
        has_published = true;

        phase_count = 0;
        phase_sum = 0;
        phase_min = std::numeric_limits<uint32_t>::max();
        phase_max = 0;
    }

    uint32_t getMemLatency(uint32_t initial) const {
        return (has_published && published_latency) ? published_latency : initial;
    }

    void updateModel(uint32_t latency) {
        phase_sum += static_cast<uint64_t>(latency);
        ++phase_count;
        if (latency < phase_min) phase_min = latency;
        if (latency > phase_max) phase_max = latency;
    }

private:
    static constexpr size_t TrimThreshold = 50;
    static constexpr double MaxRiseRatio = 0.20; // allow up to +35% per phase
    static constexpr double MaxDropRatio = 0.20; // allow up to -20% per phase
    static constexpr double MinPhaseWeight = 0.05;
    static constexpr double EffectiveWindow = 50.0;

    uint64_t phase_sum = 0;
    size_t phase_count = 0;
    uint32_t phase_min = std::numeric_limits<uint32_t>::max();
    uint32_t phase_max = 0;
    uint32_t published_latency = 0;
    bool has_published = false;
};

struct AverageStrategy {
    void reset() {
        avg_prev_weave_lat = count ? sum / count : avg_prev_weave_lat;
        sum = 0;
        count = 0;
    }

    uint32_t getMemLatency(uint32_t initial) const {
        return avg_prev_weave_lat ? avg_prev_weave_lat : initial;
    }

    void updateModel(uint32_t latency) {
        sum += latency;
        count += 1;
    }

private:
    uint32_t sum = 0;
    uint32_t count = 0;
    uint32_t avg_prev_weave_lat = 0;
};

// Exponential moving average over observed latencies within a weave.
// On reset, the last EMA value becomes the bound for the next weave.
// Smoothing factor alpha in [0,1]; higher values weight recent samples more.
struct MovingAverageStrategy {
    void reset() {
        if (curr_count == 0) {
            // no new samples: keep prior EMA if any
            return;
        }

        // window average as double
        double avg = static_cast<double>(curr_sum) / static_cast<double>(curr_count);

        if (have_prev) {
            ema = (1.0 - alpha) * ema + alpha * avg;
        } else {
            ema = avg;
            have_prev = true;
        }

        // expose rounded/clamped EMA as uint32_t for consumers
        uint64_t r = static_cast<uint64_t>(ema);
        if (r > std::numeric_limits<uint32_t>::max()) r = std::numeric_limits<uint32_t>::max();
        ema_prev_weave_lat = static_cast<uint32_t>(r);

        // reset window accumulators
        curr_sum = 0;
        curr_count = 0;
    }

    uint32_t getMemLatency(uint32_t initial) const {
        return have_prev ? ema_prev_weave_lat : initial;
    }

    void updateModel(uint32_t latency) {
        curr_sum += static_cast<uint64_t>(latency);
        ++curr_count;
    }

private:
    inline static constexpr double alpha = 0.05;

    // internal EMA in double to avoid cumulative truncation
    double ema = 0.0;
    bool have_prev = false;

    // public-facing cached bound
    uint32_t ema_prev_weave_lat = 0;

    // window accumulators
    uint64_t curr_sum = 0;
    uint32_t curr_count = 0;
};

inline IBoundMemLatencyEstimator* createEstimator(const std::string& type, const uint32_t& initial_latency) {
    if (type == "avg") {
        return new MemLatencyEstimator<AverageStrategy>(initial_latency);
    } else if (type == "wavg" || type == "windowed") {
        return new MemLatencyEstimator<WindowedAverageStrategy>(initial_latency);
    } else if (type == "mavg" || type == "ema") {
        return new MemLatencyEstimator<MovingAverageStrategy>(initial_latency);
    } else if (type == "lst") {
        return new MemLatencyEstimator<LastSeenStrategy>(initial_latency);
    } else if (type == "fix") {
        return new MemLatencyEstimator<FixedStrategy>(initial_latency);
    } else if (type == "max") {
        return new MemLatencyEstimator<MaxStrategy>(initial_latency);
    } else {
        warn("estimator type not found : %s. use `fix` model\n", type.c_str());
        return new MemLatencyEstimator<FixedStrategy>(initial_latency);
    }
}

#endif // BOUND_MEM_LATENCY_H_
