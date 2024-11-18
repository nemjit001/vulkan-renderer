#pragma

#include <chrono>

class Timer
{
public:
	using Clock = std::chrono::steady_clock;
	using Duration = std::chrono::duration<double, std::milli>;
	using TimePoint = std::chrono::time_point<Clock, Duration>;

	void tick();

	void reset();

	double deltaTimeMS();

	double timeSinceStartMS();

private:
	TimePoint m_start = Clock::now();
	TimePoint m_current = m_start;
	TimePoint m_last = m_current;
};

class RunningAverage
{
public:
	RunningAverage(uint32_t valueCount);

	void update(double value);

	double getAverage() const;

private:
	double m_alpha = 0.0;
	double m_invAlpha = 0.0;
	double m_average = 0.0;
};
